/*
 * tool_audio.c — I2S Audio: INMP441 Microphone (RX) + MAX98357A Speaker (TX)
 *
 * Hardware (ESP32-S3, full-duplex TX+RX pair on I2S_NUM_0):
 *   GPIO 38 → BCLK  (shared: INMP441 SCK + MAX98357A BCLK)
 *   GPIO 39 → WS    (shared: INMP441 WS  + MAX98357A LRC)
 *   GPIO 40 ← DIN   (INMP441 SD   → ESP32 RX data)
 *   GPIO 21 → DOUT  (ESP32 TX data → MAX98357A DIN)
 *
 * Full-duplex notes:
 *   Both channels are allocated as one TX+RX pair on I2S_NUM_0 (single
 *   i2s_new_channel call) so they share BCLK/WS — the two chips are wired
 *   to the same clock lines per .agent/ESPET_PINS.MD. Both channels stay
 *   ENABLED from init and are never toggled at runtime:
 *     - RX reads run continuously: the wake-word engine (voice_client.c)
 *       consumes the stream; command capture funnels into s_pcm_buf.
 *     - TX sits idle with auto_clear=true → outputs constant zeros (silence).
 *   Disabling either side of a duplex pair tears down the shared clock,
 *   so runtime enable/disable is avoided entirely.
 *
 * INMP441 (RX) I2S format:
 *   - Philips I2S standard
 *   - 24-bit audio data, MSB-justified, in a 32-bit I2S slot
 *   - L/R pin = GND → data appears on LEFT channel
 *   - Layout: [LEFT_32bit][RIGHT_32bit] per stereo frame
 *   - Valid bits: bits 31..8; int16 extraction: raw >> 16
 *
 * MAX98357A (TX) I2S format:
 *   - Philips I2S standard, 32-bit slot, stereo
 *   - The SD pin selects L / R / (L+R)/2 mix depending on board wiring,
 *     so every mono sample is written to BOTH slots (always audible).
 *
 * DMA sizing:
 *   data_bit_width = 32BIT, slot_mode = STEREO
 *   → i2s_channel_read returns: (frames × 2 slots × 4 bytes) per read
 *
 * Voice flow (hands-free, no button):
 *   Say "Hi ESP" → wake → "Yes?" ack → speak command → STT → agent → TTS.
 *   Implemented in voice/voice_client.c using tool_audio_rx_read().
 *
 * Verification tools (serial REPL or LLM):
 *   tool_exec audio_play_tone  "{}"   # speaker only: 440 Hz sine
 *   tool_exec audio_record_test "{}"  # mic only: timed capture + stats
 */

#include "tools/tool_audio.h"
#include "mimi_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "voice/voice_client.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "tool_audio";

/* -----------------------------------------------------------------------
 * Audio constants
 *
 * INMP441 sensitivity note:
 *   datasheet says 94 dBSPL → -26 dBFS, so a normal voice at ~1m gives
 *   roughly -30...-40 dBFS → RMS ~300–1000 in 16-bit space. Expect RMS
 *   > 100 for any audible sound in a quiet room.
 * --------------------------------------------------------------------- */
#define AUDIO_SAMPLE_RATE       16000   /* Hz — speech / Whisper-optimal */
#define AUDIO_MAX_RECORD_MS     30000   /* 30 s cap; 30000×2 bytes = 960 KB PSRAM */
#define AUDIO_MAX_SAMPLES       ((AUDIO_SAMPLE_RATE * AUDIO_MAX_RECORD_MS) / 1000)

/* DMA sizing: 4 descriptors × 512 frames.
 * At 16 kHz stereo 32-bit: 1 frame = 8 bytes.
 * One descriptor holds 512×8 = 4096 bytes ≈ 32 ms of audio. */
#define AUDIO_DMA_DESC_COUNT    4
#define AUDIO_DMA_FRAME_COUNT   512

/* Read chunk: how many frames we ask for per i2s_channel_read call.
 * Must be ≤ dma_frame_num. 256 frames = 16 ms at 16 kHz — responsive. */
#define AUDIO_READ_CHUNK_FRAMES 256
/* Stereo 32-bit: 2 slots × 4 bytes per frame */
#define AUDIO_READ_CHUNK_BYTES  (AUDIO_READ_CHUNK_FRAMES * 2 * sizeof(int32_t))

/* Read timeout passed to i2s_channel_read (ms) */
#define AUDIO_READ_TIMEOUT_MS   500

/* Debug thresholds */
#define DUMP_SAMPLE_COUNT       64
#define SILENCE_RMS_THRESHOLD   80
#define AUDIO_RMS_THRESHOLD     500

/* -----------------------------------------------------------------------
 * TX playback constants (MAX98357A on GPIO 21)
 * --------------------------------------------------------------------- */
#define AUDIO_TX_CHUNK_FRAMES   256     /* 16 ms per write chunk */
#define AUDIO_TX_CHUNK_BYTES    (AUDIO_TX_CHUNK_FRAMES * 2 * sizeof(int32_t))
#define AUDIO_TX_WRITE_TIMEOUT_MS 1000
#define AUDIO_TX_DRAIN_CHUNKS   10      /* 10 x 16 ms = 160 ms (> 4x512=128 ms DMA ring) */
#define AUDIO_TX_WAIT_WAV_MS    10000
#define AUDIO_TX_WAIT_TONE_MS   1500

#define TONE_DEFAULT_FREQ_HZ    440
#define TONE_MIN_FREQ_HZ        20
#define TONE_MAX_FREQ_HZ        8000
#define TONE_DEFAULT_MS         500
#define TONE_MAX_MS             5000
#define TONE_DEFAULT_VOL        0.5f
#define TONE_MAX_VOL            1.0f

/* -----------------------------------------------------------------------
 * I2S channel handles — full-duplex pair on I2S_NUM_0
 * --------------------------------------------------------------------- */
static i2s_chan_handle_t s_tx_handle       = NULL;
static i2s_chan_handle_t s_rx_handle       = NULL;
static bool              s_i2s_initialized = false;

/* -----------------------------------------------------------------------
 * Capture / playback state
 * --------------------------------------------------------------------- */
static portMUX_TYPE      s_audio_mux            = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_tx_mutex             = NULL; /* serializes I2S TX only */
static volatile bool     s_recording            = false;
static volatile bool     s_playing              = false;
static int               s_speaker_session_depth = 0;   /* refcounted session gate */
static bool              s_cache_clearing       = false;
/* Whole speaker-session gate: derived from s_speaker_session_depth, s_playing,
 * and s_cache_clearing under s_audio_mux. The RX feeder parks for the entire
 * session (TTS download + playback + DMA tail). */
static volatile bool s_speaker_busy           = false;

static inline void audio_sync_speaker_busy_locked(void)
{
    s_speaker_busy = (s_speaker_session_depth > 0) || s_playing || s_cache_clearing;
}
static int16_t      *s_pcm_buf                = NULL; /* PSRAM — mono 16-bit */
static size_t        s_pcm_samples_captured   = 0;
static size_t        s_capture_max            = 0; /* armed capture limit */

/* ---- RX ring buffer (fed by the high-prio mic_feeder task) ---- */
#define RX_RING_SAMPLES  65536   /* 4.096 s @16 kHz — MUST be a power of two */
#define RX_RING_MASK     (RX_RING_SAMPLES - 1)
static int16_t       *s_rx_ring      = NULL;    /* PSRAM */
static volatile size_t s_rx_ring_w   = 0;       /* write index (feeder) */
static volatile size_t s_rx_ring_r   = 0;       /* read index (consumers) */
static TaskHandle_t   s_feeder_handle = NULL;
static volatile bool  s_feeder_run    = false;
/* debug: DMA->ring health (read by voice_task's 5 s report) */
volatile uint32_t s_feeder_reads  = 0;   /* dma read calls with data */
volatile uint32_t s_feeder_zeros  = 0;   /* all-zero reads (no mic data!) */
volatile uint32_t s_ring_written  = 0;   /* samples pushed into ring */

/* Ambient noise floor (RMS) for the wake VAD, estimated HERE because this
 * task runs continuously. An estimator living in voice_task never converged:
 * each false trigger blocks that task for 1.5 s capture + seconds of STT, so
 * the field log showed the threshold crawling 420 -> 1068 over two minutes
 * while the room sat at ~9000 RMS.
 *
 * Algorithm: sliding-window MINIMUM of per-read RMS (~4 s window). The room
 * noise here is STATIONARY (fan/TV/electrical hum, ~9000 RMS steady) while
 * speech is TRANSIENT, so a minimum locks onto the stationary floor and cannot
 * be dragged up mid-utterance (the window still holds seconds of pre-speech
 * noise). It converges on the first read at boot, so a loud room is handled
 * immediately. Full-window flush (~4 s) is the cost of a sudden level change;
 * the existing deglitch filter already suppresses single-sample rail pops. */
#define MIC_FLOOR_WIN 256   /* reads ≈ 4 s of audio (256 frames/read @16 kHz) */
volatile int32_t  s_mic_floor      = 0;

static void mic_feeder_task(void *arg);
static uint32_t      s_record_start_ms        = 0;
static uint32_t      s_record_duration_ms     = 0;

/* -----------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------- */
static esp_err_t audio_play_tone_execute(const char *in, char *out, size_t out_sz);
static esp_err_t audio_record_test_execute(const char *in, char *out, size_t out_sz);
static esp_err_t audio_spktest_execute(const char *in, char *out, size_t out_sz);
static void      dump_capture_stats(void);
static void      log_gpio_states(void);
static esp_err_t play_tone(uint32_t freq_hz, uint32_t duration_ms, float vol);
static const char *mic_verdict(double rms);

/* -----------------------------------------------------------------------
 * mimi_tool_t descriptors (extern in header)
 * --------------------------------------------------------------------- */
mimi_tool_t audio_play_tone_tool = {
    .name        = "audio_play_tone",
    .description = "Play a tone through the MAX98357A speaker. Verifies audio output. "
                   "Optional params: freq_hz (default 440, 20-8000), "
                   "duration_ms (default 500, max 5000), volume (0.05-1.0, default 0.5).",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"freq_hz\":{\"type\":\"integer\",\"description\":\"Tone frequency Hz (default 440, 20-8000)\"},"
        "\"duration_ms\":{\"type\":\"integer\",\"description\":\"Duration ms (default 500, max 5000)\"},"
        "\"volume\":{\"type\":\"number\",\"description\":\"0.05-1.0 (default 0.5)\"}}"
        ",\"required\":[]}",
    .execute = audio_play_tone_execute,
};

mimi_tool_t audio_record_test_tool = {
    .name        = "audio_record_test",
    .description = "Record from INMP441 for a fixed duration and return amplitude stats as JSON. "
                   "Optional param: duration_ms (default 3000, max 10000).",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"duration_ms\":{\"type\":\"integer\","
        "\"description\":\"Recording duration ms (default 3000, max 10000)\"}}"
        ",\"required\":[]}",
    .execute = audio_record_test_execute,
};

mimi_tool_t audio_spktest_tool = {
    .name        = "audio_spktest",
    .description = "4-stage speaker diagnostic (listen and note each stage). "
                   "1: 440 Hz full-volume tone (amp/wiring/power). "
                   "2: locally generated 1 kHz WAV through the playback parser (no network). "
                   "3: volume ramp 1.0/0.4/0.15 (audibility threshold — TTS speech sits at "
                   "~6% full scale, quieter than any of these). "
                   "4: full pipeline — server TTS 'Speaker test, one, two, three'.",
    .input_schema_json = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .execute = audio_spktest_execute,
};

/* =======================================================================
 * GPIO Diagnostics
 *
 * Logs the level of every audio-related pin so we can spot wiring issues
 * without an oscilloscope. Called at init and before each capture.
 *
 * BCLK/WS are driven by the I2S controller and run permanently in
 * full-duplex mode, so their idle level reads 0 or 1 depending on where
 * in the waveform the read lands — neither is an error.
 * ===================================================================== */
static void log_gpio_states(void)
{
    int din  = gpio_get_level((gpio_num_t)MIMI_GPIO_I2S_DIN);
    int dout = gpio_get_level((gpio_num_t)MIMI_GPIO_I2S_DOUT);
    int bclk = gpio_get_level((gpio_num_t)MIMI_GPIO_I2S_BCLK);
    int ws   = gpio_get_level((gpio_num_t)MIMI_GPIO_I2S_WS);
    int btn  = -1;
#if (MIMI_GPIO_BTN_CAM >= 0)
    btn = gpio_get_level((gpio_num_t)MIMI_GPIO_BTN_CAM);
#endif

    ESP_LOGI(TAG, "+------ GPIO STATE SNAPSHOT -------------------------+");
    ESP_LOGI(TAG, "| GPIO %2d DIN  (INMP441 SD)   = %d                  |", MIMI_GPIO_I2S_DIN, din);
    ESP_LOGI(TAG, "| GPIO %2d DOUT (MAX98357A DIN) = %d                  |", MIMI_GPIO_I2S_DOUT, dout);
    ESP_LOGI(TAG, "| GPIO %2d BCLK (shared clock)  = %d  (clock runs)    |", MIMI_GPIO_I2S_BCLK, bclk);
    ESP_LOGI(TAG, "| GPIO %2d WS   (word select)   = %d  (clock runs)    |", MIMI_GPIO_I2S_WS, ws);
    ESP_LOGI(TAG, "| GPIO %2d BTN  (cam, active)  = %d  (0=pressed)     |", MIMI_GPIO_BTN_CAM, btn);
    ESP_LOGI(TAG, "+-----------------------------------------------------+");
}

/* =======================================================================
 * I2S Initialization — full-duplex (INMP441 RX + MAX98357A TX)
 * ===================================================================== */

esp_err_t tool_audio_init(void)
{
    if (s_i2s_initialized) {
        ESP_LOGW(TAG, "I2S already initialized — skipping");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "=== tool_audio_init: full-duplex I2S (INMP441 RX + MAX98357A TX) ===");
    ESP_LOGI(TAG, "Pin map:");
    ESP_LOGI(TAG, "  BCLK = GPIO %d  (shared: INMP441 SCK + MAX98357A BCLK)", MIMI_GPIO_I2S_BCLK);
    ESP_LOGI(TAG, "  WS   = GPIO %d  (shared: INMP441 WS  + MAX98357A LRC)",  MIMI_GPIO_I2S_WS);
    ESP_LOGI(TAG, "  DIN  = GPIO %d  (INMP441 SD → ESP32 RX)",                MIMI_GPIO_I2S_DIN);
    ESP_LOGI(TAG, "  DOUT = GPIO %d  (ESP32 TX → MAX98357A DIN)",             MIMI_GPIO_I2S_DOUT);
    ESP_LOGI(TAG, "Config: %d Hz | 32-bit slot | stereo | Philips std",      AUDIO_SAMPLE_RATE);
    ESP_LOGI(TAG, "DMA: %d descriptors × %d frames (%d ms per desc)",
             AUDIO_DMA_DESC_COUNT, AUDIO_DMA_FRAME_COUNT,
             (AUDIO_DMA_FRAME_COUNT * 1000) / AUDIO_SAMPLE_RATE);

    /* Log pin states BEFORE touching I2S */
    log_gpio_states();

    /* ------------------------------------------------------------------
     * Step 1: Allocate the TX+RX pair on I2S_NUM_0 in one call.
     * Passing both handles = full-duplex: both channels share the BCLK/WS
     * signal generator, matching the shared-clock wiring of the two chips.
     * auto_clear=true makes the TX channel output constant zeros whenever
     * no data is queued → digital silence while idle.
     * ------------------------------------------------------------------ */
    i2s_chan_config_t chan_cfg = {
        .id            = I2S_NUM_0,
        .role          = I2S_ROLE_MASTER,   /* ESP32 generates BCLK + WS */
        .dma_desc_num  = AUDIO_DMA_DESC_COUNT,
        .dma_frame_num = AUDIO_DMA_FRAME_COUNT,
        .auto_clear    = true,
    };

    ESP_LOGI(TAG, "[1/4] Allocating I2S_NUM_0 TX+RX full-duplex pair...");
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_handle, &s_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel FAILED: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "  Possible causes: another driver already using I2S_NUM_0, "
                      "or insufficient memory.");
        return err;
    }
    ESP_LOGI(TAG, "[1/4] OK — TX handle: %p | RX handle: %p",
             (void *)s_tx_handle, (void *)s_rx_handle);

    /* ------------------------------------------------------------------
     * Step 2a: RX channel config — INMP441 capture (unchanged, proven).
     *   32-bit slots, stereo frame, LEFT slot carries data (L/R=GND).
     * ------------------------------------------------------------------ */
    i2s_std_config_t rx_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .slot_mode      = I2S_SLOT_MODE_STEREO,
            .slot_mask      = I2S_STD_SLOT_LEFT,   /* INMP441 L/R=GND → left */
            .ws_width       = 32,
            .ws_pol         = false,
            .bit_shift      = true,
            .left_align     = false,
            .big_endian     = false,
            .bit_order_lsb  = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIMI_GPIO_I2S_BCLK,
            .ws   = MIMI_GPIO_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIMI_GPIO_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    /* ------------------------------------------------------------------
     * Step 2b: TX channel config — MAX98357A playback.
     *   Same clocks/rate as RX; mono samples are duplicated into BOTH
     *   slots at write time (SD pin may select L, R or (L+R)/2).
     * ------------------------------------------------------------------ */
    i2s_std_config_t tx_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .slot_mode      = I2S_SLOT_MODE_STEREO,
            .slot_mask      = I2S_STD_SLOT_BOTH,
            .ws_width       = 32,
            .ws_pol         = false,
            .bit_shift      = true,
            .left_align     = false,
            .big_endian     = false,
            .bit_order_lsb  = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIMI_GPIO_I2S_BCLK,
            .ws   = MIMI_GPIO_I2S_WS,
            .dout = MIMI_GPIO_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_LOGI(TAG, "[2/4] Configuring I2S STD Philips mode (TX + RX)...");
    err = i2s_channel_init_std_mode(s_tx_handle, &tx_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TX i2s_channel_init_std_mode FAILED: %s", esp_err_to_name(err));
        goto fail;
    }
    err = i2s_channel_init_std_mode(s_rx_handle, &rx_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RX i2s_channel_init_std_mode FAILED: %s", esp_err_to_name(err));
        goto fail;
    }
    ESP_LOGI(TAG, "[2/4] OK — Philips 32-bit stereo configured on both channels");

    /* ------------------------------------------------------------------
     * Step 3: Allocate PSRAM capture buffer.
     * 16000 samples/s × 30 s × 2 bytes = 960 KB.
     * ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "[3/4] Allocating PCM buffer (%u KB in PSRAM)...",
             (unsigned)(AUDIO_MAX_SAMPLES * sizeof(int16_t) / 1024));
    s_pcm_buf = (int16_t *)heap_caps_malloc(
        AUDIO_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_pcm_buf) {
        ESP_LOGW(TAG, "PSRAM alloc failed — trying internal RAM");
        s_pcm_buf = (int16_t *)malloc(AUDIO_MAX_SAMPLES * sizeof(int16_t));
    }
    if (!s_pcm_buf) {
        ESP_LOGE(TAG, "PCM buffer alloc FAILED (%u bytes) — out of memory",
                 (unsigned)(AUDIO_MAX_SAMPLES * sizeof(int16_t)));
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    ESP_LOGI(TAG, "[3/4] OK — PCM buffer at %p (%u samples max)",
             (void *)s_pcm_buf, (unsigned)AUDIO_MAX_SAMPLES);

    /* ------------------------------------------------------------------
     * Step 4: Enable BOTH channels once. TX starts clocking immediately
     * (outputs silence via auto_clear); RX DMA starts filling. Neither
     * channel is ever disabled at runtime — see header comment.
     * ------------------------------------------------------------------ */
    err = i2s_channel_enable(s_tx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(TX) FAILED: %s", esp_err_to_name(err));
        goto fail;
    }
    err = i2s_channel_enable(s_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(RX) FAILED: %s", esp_err_to_name(err));
        i2s_channel_disable(s_tx_handle);
        goto fail;
    }
    if (!s_tx_mutex) {
        s_tx_mutex = xSemaphoreCreateMutex();
        if (!s_tx_mutex) {
            ESP_LOGE(TAG, "Failed to create TX mutex");
            i2s_channel_disable(s_rx_handle);
            i2s_channel_disable(s_tx_handle);
            err = ESP_ERR_NO_MEM;
            goto fail;
        }
    }
    s_i2s_initialized = true;
    ESP_LOGI(TAG, "[4/4] tool_audio_init COMPLETE — TX and RX enabled");
    ESP_LOGI(TAG, "  Say \"Hi ESP\" to wake the voice assistant.");
    ESP_LOGI(TAG, "  Serial REPL: tool_exec audio_play_tone \"{}\" to test the speaker.");
    return ESP_OK;

fail:
    if (s_rx_handle) { i2s_del_channel(s_rx_handle); s_rx_handle = NULL; }
    if (s_tx_handle) { i2s_del_channel(s_tx_handle); s_tx_handle = NULL; }
    if (s_pcm_buf)   { free(s_pcm_buf); s_pcm_buf = NULL; }
    return err;
}

/* =======================================================================
 * Tone generator helpers
 * ===================================================================== */

/* Fill a TX chunk with a stereo-duplicated sine. Mono sample goes to both
 * slots so the MAX98357A hears it regardless of SD-pin channel select. */
static void tone_fill_chunk(int32_t *chunk, size_t frames,
                            double *phase, double inc, float vol)
{
    for (size_t f = 0; f < frames; f++) {
        int32_t v = (int32_t)(sin(*phase) * (double)vol * 2147483000.0);
        *phase += inc;
        if (*phase >= 2.0 * M_PI) {
            *phase -= 2.0 * M_PI;
        }
        chunk[f * 2]     = v;   /* left slot  */
        chunk[f * 2 + 1] = v;   /* right slot */
    }
}

/* Drain TX DMA ring with silence so mic_feeder_task never unparks while
 * the final audio descriptors are still shifting out to MAX98357A. */
static void audio_tx_drain_dma(int32_t *zero_chunk)
{
    if (!s_tx_handle || !zero_chunk) {
        return;
    }
    memset(zero_chunk, 0, AUDIO_TX_CHUNK_BYTES);
    for (int i = 0; i < AUDIO_TX_DRAIN_CHUNKS; i++) {
        size_t written = 0;
        if (i2s_channel_write(s_tx_handle, zero_chunk, AUDIO_TX_CHUNK_BYTES,
                              &written, AUDIO_TX_WRITE_TIMEOUT_MS) != ESP_OK) {
            break;
        }
    }
}

/* Generate and play a sine tone. Blocking for duration_ms.
 * Serialized by s_tx_mutex and gated via s_audio_mux so concurrent callers
 * cannot interleave writes or clobber an active speaker session. */
static esp_err_t play_tone_wait(uint32_t freq_hz, uint32_t duration_ms,
                                float vol, uint32_t wait_ms)
{
    if (!s_i2s_initialized || !s_tx_handle || !s_tx_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (freq_hz < TONE_MIN_FREQ_HZ || freq_hz > TONE_MAX_FREQ_HZ) {
        return ESP_ERR_INVALID_ARG;
    }
    if (duration_ms == 0 || duration_ms > TONE_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!(vol > 0.0f && vol <= TONE_MAX_VOL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "play_tone: TX mutex busy (wait=%lu ms)", (unsigned long)wait_ms);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_recording) {
        ESP_LOGW(TAG, "play_tone: busy (recording=1)");
        xSemaphoreGive(s_tx_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&s_audio_mux);
    s_playing = true;
    audio_sync_speaker_busy_locked();
    taskEXIT_CRITICAL(&s_audio_mux);

    int32_t *chunk = (int32_t *)malloc(AUDIO_TX_CHUNK_BYTES);
    if (!chunk) {
        taskENTER_CRITICAL(&s_audio_mux);
        s_playing = false;
        audio_sync_speaker_busy_locked();
        taskEXIT_CRITICAL(&s_audio_mux);
        xSemaphoreGive(s_tx_mutex);
        return ESP_ERR_NO_MEM;
    }

    double inc   = 2.0 * M_PI * (double)freq_hz / (double)AUDIO_SAMPLE_RATE;
    double phase = 0.0;
    size_t total_frames = (size_t)((uint64_t)duration_ms * AUDIO_SAMPLE_RATE / 1000);
    size_t played_frames = 0;
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "[SPK] Playing %lu Hz for %lu ms (vol %.2f) on GPIO %d",
             (unsigned long)freq_hz, (unsigned long)duration_ms, vol, MIMI_GPIO_I2S_DOUT);

    while (played_frames < total_frames) {
        size_t frames = total_frames - played_frames;
        if (frames > AUDIO_TX_CHUNK_FRAMES) {
            frames = AUDIO_TX_CHUNK_FRAMES;
        }
        tone_fill_chunk(chunk, frames, &phase, inc, vol);

        size_t written = 0;
        err = i2s_channel_write(s_tx_handle, chunk,
                                frames * 2 * sizeof(int32_t), &written,
                                AUDIO_TX_WRITE_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write FAILED: %s", esp_err_to_name(err));
            break;
        }
        played_frames += written / (2 * sizeof(int32_t));
    }

    if (err == ESP_OK) {
        audio_tx_drain_dma(chunk);
    }

    free(chunk);
    taskENTER_CRITICAL(&s_audio_mux);
    s_playing = false;
    audio_sync_speaker_busy_locked();
    taskEXIT_CRITICAL(&s_audio_mux);
    xSemaphoreGive(s_tx_mutex);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[SPK] Tone done (%u frames)", (unsigned)played_frames);
    }
    return err;
}

static esp_err_t play_tone(uint32_t freq_hz, uint32_t duration_ms, float vol)
{
    return play_tone_wait(freq_hz, duration_ms, vol, AUDIO_TX_WAIT_TONE_MS);
}

/* Capture is handled directly by mic_feeder_task (single I2S DMA reader) */
/* =======================================================================
 * Mic feeder task — drains the I2S DMA into the RX ring buffer.
 *
 * DMA holds only ~128 ms (4 × 512-frame descriptors); consumers that don't
 * run continuously (voice_wake at low priority) overflow it and lose audio.
 * This task runs at prio 7 on core 1 and only shovels bytes (each read is
 * up to 32 ms of audio, immediately buffered), so other tasks are barely
 * affected while the ring guarantees the wake engine a gap-free stream.
 * ===================================================================== */
static void mic_feeder_task(void *arg)
{
    ESP_LOGI(TAG, "mic_feeder_task started (chunk=%u bytes, timeout=%d ms)",
             (unsigned)AUDIO_READ_CHUNK_BYTES, AUDIO_READ_TIMEOUT_MS);
    static int32_t dma_buf[AUDIO_READ_CHUNK_FRAMES * 2];
    while (s_feeder_run) {
        /* Park during a speaker session or standalone recording test: I2S TX
         * needs the bus/CPU, or audio_record_test owns RX DMA. */
        if (s_playing || s_speaker_busy) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(s_rx_handle, dma_buf, sizeof(dma_buf),
                                         &bytes_read, pdMS_TO_TICKS(AUDIO_READ_TIMEOUT_MS));
        if (bytes_read == 0) {
            static int64_t s_last_feeder_err = 0;
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - s_last_feeder_err >= 5000) {
                s_last_feeder_err = now_ms;
                ESP_LOGW(TAG, "[feeder] i2s_channel_read failed: %s (bytes_read=0)",
                         esp_err_to_name(err));
            }
            vTaskDelay(1);
            continue;
        }
        size_t frames = bytes_read / (2 * sizeof(int32_t));
        s_feeder_reads++;
        int32_t frame_energy = 0;
        int64_t sumsq = 0;
        int64_t sumsq_l = 0;
        int64_t sumsq_r = 0;
        static int16_t s_prev_sample = 0;
        static int32_t s_dc_x_prev = 0;
        static int32_t s_dc_y_prev = 0;
        static int s_active_slot = 0; /* 0 = Left (L/R=GND), 1 = Right (L/R=3.3V/float) */

        for (size_t f = 0; f < frames; f++) {
            int32_t raw_l = (int32_t)(dma_buf[f * 2] >> 16);
            int32_t raw_r = (int32_t)(dma_buf[f * 2 + 1] >> 16);
            sumsq_l += (int64_t)raw_l * (int64_t)raw_l;
            sumsq_r += (int64_t)raw_r * (int64_t)raw_r;

            int32_t raw = (s_active_slot == 1) ? raw_r : raw_l;
            int32_t boosted = raw;
            if (boosted > 32767) boosted = 32767;
            else if (boosted < -32768) boosted = -32768;

            /* 1st-order DC-blocking high-pass filter (fc ≈ 20Hz at 16kHz):
             * y[n] = x[n] - x[n-1] + (254/256) * y[n-1]
             * Removes hardware DC offset, ground-bounce & OLED charge-pump DC ripple */
            int32_t y_hp = boosted - s_dc_x_prev + ((s_dc_y_prev * 254) >> 8);
            s_dc_x_prev = boosted;
            if (y_hp > 32767) y_hp = 32767;
            else if (y_hp < -32768) y_hp = -32768;
            s_dc_y_prev = y_hp;
            int16_t s = (int16_t)y_hp;

            /* Deglitch filter: suppress single-sample full-scale rail pops */
            int32_t diff = (int32_t)s - (int32_t)s_prev_sample;
            if ((s >= 32000 || s <= -32000) && (diff > 20000 || diff < -20000)) {
                s = s_prev_sample;
            } else {
                s_prev_sample = s;
            }

            frame_energy += (s < 0 ? -s : s);
            sumsq += (int64_t)s * (int64_t)s;
            size_t used = (s_rx_ring_w - s_rx_ring_r) & RX_RING_MASK;
            if (used < RX_RING_SAMPLES - 1) {
                s_rx_ring[s_rx_ring_w] = s;
                s_rx_ring_w = (s_rx_ring_w + 1) & RX_RING_MASK;
                s_ring_written++;
            }
            if (s_recording && s_pcm_buf && s_pcm_samples_captured < s_capture_max) {
                s_pcm_buf[s_pcm_samples_captured++] = s;
            }
        }

        /* Check if INMP441 L/R pin is swapped (e.g. L/R pulled to VDD or floating) */
        if (frames > 0) {
            int32_t rms_l = (int32_t)sqrt((double)sumsq_l / (double)frames);
            int32_t rms_r = (int32_t)sqrt((double)sumsq_r / (double)frames);
            static int64_t s_last_lr_check = 0;
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - s_last_lr_check >= 5000) {
                s_last_lr_check = now_ms;
                if (s_active_slot == 0 && rms_l > 25000 && rms_r > 300 && rms_r < 20000) {
                    ESP_LOGW(TAG, "MIC AUTO-SWITCH: Left channel railed (rms=%d) but Right channel healthy (rms=%d). "
                                  "INMP441 L/R pin is floating or on 3.3V -> auto-switched to Right channel",
                             (int)rms_l, (int)rms_r);
                    s_active_slot = 1;
                } else if (s_active_slot == 1) {
                    /* Restore to Left channel if Right channel rails or if Left channel is healthy while Right is silent */
                    if ((rms_r > 25000 && rms_l > 300 && rms_l < 20000) ||
                        (rms_l > 300 && rms_l < 20000 && (rms_r < 200 || rms_r > 25000))) {
                        ESP_LOGW(TAG, "MIC AUTO-SWITCH: Left channel healthy (rms=%d, right=%d) -> restored to Left channel",
                                 (int)rms_l, (int)rms_r);
                        s_active_slot = 0;
                    }
                }
            }
        }
        if (frame_energy == 0) {
            s_feeder_zeros++;   /* whole read was exact zeros — mic not clocking! */
        }

        /* Ambient floor update (see s_mic_floor comment): sliding-window
         * minimum of per-read RMS. Runs every ~16 ms of audio regardless of
         * what voice_task is doing, so the wake VAD's threshold is always
         * current — including while a capture or STT call blocks the listener. */
        if (frames > 0) {
            int32_t rms = (int32_t)sqrt((double)sumsq / (double)frames);
            if (rms > 0) {
                static int32_t win[MIC_FLOOR_WIN];
                static int win_pos = 0, win_n = 0;
                win[win_pos] = rms;
                win_pos = (win_pos + 1) % MIC_FLOOR_WIN;
                if (win_n < MIC_FLOOR_WIN) win_n++;
                int32_t mn = win[0];
                for (int i = 1; i < win_n; i++) {
                    if (win[i] < mn) mn = win[i];
                }
                s_mic_floor = mn;
            }
        }
        /* Pace: each read grabs ~32 ms of audio instantly (DMA is always
         * full). Without a yield this task spins at prio 7 and starves
         * IDLE1 (field WDT). One tick per 32 ms still consumes 3x realtime,
         * and the 4 s ring absorbs the jitter. */
        vTaskDelay(1);
    }
    s_feeder_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t tool_audio_rx_stream_start(void)
{
    if (!s_i2s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_feeder_handle) {
        return ESP_OK;   /* already running */
    }
    if (!s_rx_ring) {
        s_rx_ring = heap_caps_calloc(RX_RING_SAMPLES, sizeof(int16_t),
                                     MALLOC_CAP_SPIRAM);
        if (!s_rx_ring) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_rx_ring_w = s_rx_ring_r = 0;
    s_feeder_run = true;

    /* Internal heap is too fragmented after WiFi/TLS/telegram for a dynamic
     * task stack (field: xTaskCreate fails). Static task with a PSRAM stack.
     * Size note: the first 8 KB attempt overflowed (kernel list corruption +
     * StoreProhibited in vTaskDelay — i2s_channel_read's TLS/queue call chain
     * is deep); 12 KB in PSRAM is cheap and safe. */
    static StackType_t *feeder_stack = NULL;
    static StaticTask_t feeder_tcb;
    if (!feeder_stack) {
        feeder_stack = heap_caps_malloc(3072 * sizeof(StackType_t),
                                        MALLOC_CAP_SPIRAM);
        if (!feeder_stack) {
            s_feeder_run = false;
            return ESP_ERR_NO_MEM;
        }
    }
    s_feeder_handle = xTaskCreateStatic(mic_feeder_task, "mic_feed",
                                        3072, NULL, 7,
                                        feeder_stack, &feeder_tcb);
    if (!s_feeder_handle) {
        s_feeder_run = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "mic feeder started (4 s ring + 12 KB PSRAM stack)");
    return ESP_OK;
}

void tool_audio_rx_stream_stop(void)
{
    s_feeder_run = false;
    /* task self-deletes; do not wait here */
}

/* Pull up to max_out samples from the ring. Returns samples written. */
size_t tool_audio_ring_read(int16_t *out, size_t max_out, uint32_t timeout_ms)
{
    size_t got = 0;
    int64_t deadline = (int64_t)(esp_timer_get_time() / 1000) + timeout_ms;
    while (got < max_out) {
        if (s_rx_ring_w == s_rx_ring_r) {
            if (got > 0 ||
                (int64_t)(esp_timer_get_time() / 1000) >= deadline) {
                break;
            }
            vTaskDelay(1);
            continue;
        }
        out[got++] = s_rx_ring[s_rx_ring_r];
        s_rx_ring_r = (s_rx_ring_r + 1) & RX_RING_MASK;
    }
    return got;
}

size_t tool_audio_ring_available(void)
{
    return (s_rx_ring_w - s_rx_ring_r) & RX_RING_MASK;
}

void tool_audio_ring_flush(void)
{
    s_rx_ring_r = s_rx_ring_w;   /* drop everything buffered */
}

/* Drain any queued RX data so a capture starts "now", not with
 * audio buffered while the bus was idle or speaker was active. */
void tool_audio_rx_flush(void)
{
    if (!s_rx_handle) {
        return;
    }
    /* Single DMA owner is mic_feeder_task — flush ring buffer without touching DMA */
    tool_audio_ring_flush();
}

/* Clear all audio caches (drains both I2S hardware DMA FIFO and software ring buffer) */
void tool_audio_clear_cache(void)
{
    if (!s_i2s_initialized) {
        return;
    }
    taskENTER_CRITICAL(&s_audio_mux);
    s_cache_clearing = true;
    audio_sync_speaker_busy_locked();
    taskEXIT_CRITICAL(&s_audio_mux);

    vTaskDelay(pdMS_TO_TICKS(15));
    tool_audio_rx_flush();
    tool_audio_ring_flush();

    taskENTER_CRITICAL(&s_audio_mux);
    s_cache_clearing = false;
    audio_sync_speaker_busy_locked();
    taskEXIT_CRITICAL(&s_audio_mux);
}

void tool_audio_speaker_session_begin(void)
{
    taskENTER_CRITICAL(&s_audio_mux);
    s_speaker_session_depth++;
    audio_sync_speaker_busy_locked();
    taskEXIT_CRITICAL(&s_audio_mux);
}

void tool_audio_speaker_session_end(void)
{
    bool reached_zero = false;
    taskENTER_CRITICAL(&s_audio_mux);
    if (s_speaker_session_depth > 0) {
        s_speaker_session_depth--;
    }
    reached_zero = (s_speaker_session_depth == 0);
    audio_sync_speaker_busy_locked();
    taskEXIT_CRITICAL(&s_audio_mux);

    if (reached_zero) {
        tool_audio_rx_flush();
        tool_audio_ring_flush();
    }
}

/* =======================================================================
 * Timed capture helper — used by audio_record_test
 *
 * Starts capture_task, lets it run for duration_ms, stops it, and returns
 * amplitude statistics of what was captured.
 * ===================================================================== */
static esp_err_t timed_capture(uint32_t duration_ms, size_t *out_n, uint32_t *out_dur,
                               double *out_rms, int16_t *out_min, int16_t *out_max)
{
    if (!s_i2s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_recording || s_playing) {
        return ESP_ERR_INVALID_STATE;
    }

    tool_audio_clear_cache();
    s_pcm_samples_captured = 0;
    s_capture_max = (duration_ms * (AUDIO_SAMPLE_RATE / 1000));
    if (s_capture_max > AUDIO_MAX_SAMPLES) s_capture_max = AUDIO_MAX_SAMPLES;
    s_record_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_recording = true;

    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    s_recording = false;
    s_record_duration_ms = (uint32_t)(esp_timer_get_time() / 1000) - s_record_start_ms;

    size_t n = s_pcm_samples_captured;
    int16_t mn = INT16_MAX, mx = INT16_MIN;
    int64_t ssq = 0;
    for (size_t i = 0; i < n; i++) {
        int16_t s = s_pcm_buf[i];
        if (s < mn) mn = s;
        if (s > mx) mx = s;
        ssq += (int64_t)s * s;
    }

    if (out_n)   *out_n   = n;
    if (out_dur) *out_dur = s_record_duration_ms;
    if (out_min) *out_min = mn;
    if (out_max) *out_max = mx;
    if (out_rms) *out_rms = (n > 0) ? sqrt((double)ssq / (double)n) : 0.0;
    return ESP_OK;
}

/* =======================================================================
 * Capture control API (used by the wake-word voice flow, voice_client.c)
 * ===================================================================== */

bool tool_audio_is_busy(void)
{
    if (s_recording && s_record_start_ms > 0) {
        uint32_t elapsed_ms = (uint32_t)(esp_timer_get_time() / 1000) - s_record_start_ms;
        if (elapsed_ms > AUDIO_MAX_RECORD_MS + 2000) {
            ESP_LOGW(TAG, "s_recording active for %u ms without stop — force disarming", (unsigned)elapsed_ms);
            tool_audio_capture_disarm();
        }
    }
    return s_recording || s_playing;
}

bool tool_audio_is_speaker_busy(void)
{
    bool busy;
    taskENTER_CRITICAL(&s_audio_mux);
    busy = s_playing || s_speaker_busy;
    taskEXIT_CRITICAL(&s_audio_mux);
    return busy;
}


/* Start capturing into s_pcm_buf WITHOUT a worker task: the caller's task
 * pumps tool_audio_rx_read() itself (the wake listener loop already exists,
 * no need for a second task). Returns false if busy. */
bool tool_audio_capture_arm(size_t max_samples)
{
    return tool_audio_capture_arm_preroll(max_samples, 0);
}

bool tool_audio_capture_arm_preroll(size_t max_samples, size_t preroll_samples)
{
    if (!s_i2s_initialized || s_recording || s_playing) {
        return false;
    }
    s_pcm_samples_captured = 0;
    s_capture_max = (max_samples > AUDIO_MAX_SAMPLES) ? AUDIO_MAX_SAMPLES : max_samples;
    s_record_start_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (preroll_samples == 0) {
        tool_audio_ring_flush();   /* stale buffered audio out; capture starts now */
    } else if (s_rx_ring) {
        if (preroll_samples > RX_RING_SAMPLES / 2) preroll_samples = RX_RING_SAMPLES / 2;
        size_t r = (s_rx_ring_w >= preroll_samples)
                      ? (s_rx_ring_w - preroll_samples)
                      : (RX_RING_SAMPLES - (preroll_samples - s_rx_ring_w));
        r &= RX_RING_MASK;
        while (r != s_rx_ring_w && s_pcm_samples_captured < s_capture_max) {
            s_pcm_buf[s_pcm_samples_captured++] = s_rx_ring[r];
            r = (r + 1) & RX_RING_MASK;
        }
        s_rx_ring_r = s_rx_ring_w;
    }
    s_recording = true;
    return true;
}

/* Pump one RX chunk. Works in TWO modes:
 *   - armed (s_recording): samples are copied to out AND accumulated into
 *     s_pcm_buf until s_capture_max; returns false when the limit is hit.
 *   - listening (!s_recording, wake-word mode): samples are only copied to
 *     out; always returns true (nothing to fill). */
bool tool_audio_rx_read(int16_t *out, size_t max_out, size_t *out_written,
                        size_t *captured_total, bool *out_full, uint32_t timeout_ms)
{
    *out_written = 0;
    if (out_full) *out_full = false;
    if (!s_rx_ring) {
        return s_recording;
    }

    /* Both modes consume the RING — the prio-7 feeder is the single DMA
     * owner (direct DMA reads here used to race it: dual consumers each got
     * half the audio). Armed mode additionally mirrors into s_pcm_buf. */
    int64_t deadline = (int64_t)(esp_timer_get_time() / 1000) + timeout_ms;
    while (*out_written < max_out) {
        if (s_rx_ring_w == s_rx_ring_r) {
            if (*out_written > 0 ||
                (int64_t)(esp_timer_get_time() / 1000) >= deadline) {
                break;
            }
            vTaskDelay(1);
            continue;
        }
        int16_t s = s_rx_ring[s_rx_ring_r];
        s_rx_ring_r = (s_rx_ring_r + 1) & RX_RING_MASK;
        if (out) {
            out[(*out_written)++] = s;
        } else {
            (*out_written)++;
        }
        if (s_recording) {
            if (s_pcm_samples_captured >= s_capture_max) {
                if (out_full) *out_full = true;
                break;
            }
        } else if (captured_total) {
            *captured_total = 0;
        }
    }
    if (s_recording && captured_total) {
        *captured_total = s_pcm_samples_captured;
    }
    return s_recording ? (s_pcm_samples_captured < s_capture_max) : true;
}

/* Stop an armed capture and stamp the duration (results available via
 * tool_audio_get_last_capture). */
void tool_audio_capture_disarm(void)
{
    if (!s_recording) {
        return;
    }
    s_recording = false;
    s_record_duration_ms = (uint32_t)(esp_timer_get_time() / 1000) - s_record_start_ms;
    s_capture_max = AUDIO_MAX_SAMPLES;
}

/* =======================================================================
 * Accessor for captured PCM
 * ===================================================================== */

const int16_t *tool_audio_get_last_capture(size_t *out_samples, uint32_t *out_duration_ms)
{
    if (out_samples)     *out_samples     = s_pcm_samples_captured;
    if (out_duration_ms) *out_duration_ms = s_record_duration_ms;
    return (s_pcm_samples_captured > 0) ? s_pcm_buf : NULL;
}

esp_err_t tool_audio_manual_record_start(void)
{
    if (!s_i2s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_playing) {
        return ESP_ERR_INVALID_STATE;
    }
    /* If background VAD/capture was active, disarm it so manual button record takes priority */
    if (s_recording) {
        tool_audio_capture_disarm();
    }

    tool_audio_clear_cache();
    s_pcm_samples_captured = 0;
    s_capture_max = AUDIO_MAX_SAMPLES;
    s_record_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_recording = true;
    ESP_LOGI(TAG, "Manual recording started");
    return ESP_OK;
}

esp_err_t tool_audio_manual_record_stop(size_t *out_samples, uint32_t *out_dur_ms, double *out_rms)
{
    if (!s_recording) {
        return ESP_ERR_INVALID_STATE;
    }
    s_recording = false;
    s_record_duration_ms = (uint32_t)(esp_timer_get_time() / 1000) - s_record_start_ms;

    size_t n = s_pcm_samples_captured;
    int64_t ssq = 0;
    for (size_t i = 0; i < n; i++) {
        int16_t s = s_pcm_buf[i];
        ssq += (int64_t)s * s;
    }

    if (out_samples) *out_samples = n;
    if (out_dur_ms)  *out_dur_ms  = s_record_duration_ms;
    if (out_rms)     *out_rms     = (n > 0) ? sqrt((double)ssq / (double)n) : 0.0;
    return ESP_OK;
}

/* =======================================================================
 * Serial Diagnostic Dump — called after mic tests
 * ===================================================================== */

static const char *mic_verdict(double rms)
{
    if (rms < SILENCE_RMS_THRESHOLD) return "silence";
    if (rms < AUDIO_RMS_THRESHOLD)   return "low_signal";
    return "audio_detected";
}

static void dump_capture_stats(void)
{
    size_t n = s_pcm_samples_captured;

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           AUDIO CAPTURE DIAGNOSTICS                 ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════╝");

    if (n == 0) {
        ESP_LOGE(TAG, "  RESULT: 0 samples captured!");
        ESP_LOGE(TAG, "  Diagnosis checklist:");
        ESP_LOGE(TAG, "  [ ] INMP441 VCC → 3.3V (NOT 5V — mic is 3.3V only)");
        ESP_LOGE(TAG, "  [ ] INMP441 GND → GND");
        ESP_LOGE(TAG, "  [ ] INMP441 L/R → GND (for left channel)");
        ESP_LOGE(TAG, "  [ ] INMP441 SCK ← GPIO %d (BCLK from ESP32)", MIMI_GPIO_I2S_BCLK);
        ESP_LOGE(TAG, "  [ ] INMP441 WS  ← GPIO %d (WS from ESP32)", MIMI_GPIO_I2S_WS);
        ESP_LOGE(TAG, "  [ ] INMP441 SD  → GPIO %d (data TO ESP32)", MIMI_GPIO_I2S_DIN);
        ESP_LOGE(TAG, "  [ ] I2S pins not shared with another peripheral");
        return;
    }

    /* Statistics */
    int16_t min_amp = INT16_MAX, max_amp = INT16_MIN;
    int64_t sum_sq  = 0, dc_sum = 0;
    for (size_t i = 0; i < n; i++) {
        int16_t s = s_pcm_buf[i];
        if (s < min_amp) min_amp = s;
        if (s > max_amp) max_amp = s;
        sum_sq += (int64_t)s * s;
        dc_sum += s;
    }
    double rms       = sqrt((double)sum_sq / (double)n);
    double dc_offset = (double)dc_sum / (double)n;
    int32_t peak_pp  = (int32_t)max_amp - (int32_t)min_amp;

    ESP_LOGI(TAG, "  Duration   : %u ms", (unsigned)s_record_duration_ms);
    ESP_LOGI(TAG, "  Samples    : %u @ %d Hz (mono 16-bit PCM)", (unsigned)n, AUDIO_SAMPLE_RATE);
    ESP_LOGI(TAG, "  Buffer     : %u bytes", (unsigned)(n * sizeof(int16_t)));
    ESP_LOGI(TAG, "  Min amp    : %d (0x%04X)", (int)min_amp, (uint16_t)min_amp);
    ESP_LOGI(TAG, "  Max amp    : %d (0x%04X)", (int)max_amp, (uint16_t)max_amp);
    ESP_LOGI(TAG, "  Peak-peak  : %d", (int)peak_pp);
    ESP_LOGI(TAG, "  RMS energy : %.1f", rms);
    ESP_LOGI(TAG, "  DC offset  : %.1f", dc_offset);

    /* Verdict */
    if (rms < SILENCE_RMS_THRESHOLD) {
        ESP_LOGW(TAG, "  VERDICT: SILENCE / NO SIGNAL (RMS=%.1f < %d)", rms, SILENCE_RMS_THRESHOLD);
        ESP_LOGW(TAG, "    Mic is wired and data is flowing (samples received),");
        ESP_LOGW(TAG, "    but signal is flat. Possible causes:");
        ESP_LOGW(TAG, "    - All samples are 0x0000 → check INMP441 L/R pin (must be GND)");
        ESP_LOGW(TAG, "    - Extremely quiet environment");
        ESP_LOGW(TAG, "    - INMP441 powered but SD line not connected (reads as 0)");
    } else if (rms < AUDIO_RMS_THRESHOLD) {
        ESP_LOGW(TAG, "  VERDICT: LOW SIGNAL (RMS=%.1f) — whisper/noise?", rms);
        ESP_LOGW(TAG, "    Try speaking closer to the mic (within 10 cm).");
    } else {
        ESP_LOGI(TAG, "  VERDICT: *** AUDIO DETECTED *** (RMS=%.1f > %d) — mic working!",
                 rms, AUDIO_RMS_THRESHOLD);
    }

    /* Check if all samples are identical (stuck at 0 or constant) */
    bool all_zero = (min_amp == 0 && max_amp == 0);
    bool all_same = (min_amp == max_amp);
    if (all_zero) {
        ESP_LOGE(TAG, "  !! All samples are 0x0000 — DIN line is floating or "
                      "INMP441 L/R is not GND (data always on left, must be GND).");
    } else if (all_same) {
        ESP_LOGW(TAG, "  !! All samples identical (0x%04X) — signal stuck.", (uint16_t)min_amp);
    }

    /* Hex + decimal dump of first N samples */
    size_t dump_n = (n < DUMP_SAMPLE_COUNT) ? n : DUMP_SAMPLE_COUNT;
    ESP_LOGI(TAG, "  ── First %u samples (hex | decimal) ──", (unsigned)dump_n);
    char line[120];
    for (size_t i = 0; i < dump_n; i++) {
        if ((i % 8) == 0) {
            snprintf(line, sizeof(line), "  [%3zu] ", i);
        }
        char tmp[20];
        snprintf(tmp, sizeof(tmp), "%04X(%6d) ", (uint16_t)s_pcm_buf[i], (int)s_pcm_buf[i]);
        strncat(line, tmp, sizeof(line) - strlen(line) - 1);
        if ((i % 8) == 7 || i == dump_n - 1) {
            ESP_LOGI(TAG, "%s", line);
        }
    }

    ESP_LOGI(TAG, "  ── To analyse in Audacity / SoX ──");
    ESP_LOGI(TAG, "  File: raw PCM | signed 16-bit LE | mono | %d Hz", AUDIO_SAMPLE_RATE);
    ESP_LOGI(TAG, "════════════════════════════════════════════════════════");
}

/* =======================================================================
 * mimi_tool_t Execute functions
 * ===================================================================== */

/* audio_play_tone — real sine playback through the MAX98357A */
static esp_err_t audio_play_tone_execute(const char *input_json, char *output, size_t output_size)
{
    uint32_t freq_hz = TONE_DEFAULT_FREQ_HZ;
    uint32_t duration_ms = TONE_DEFAULT_MS;
    float vol = TONE_DEFAULT_VOL;

    if (input_json) {
        cJSON *j = cJSON_Parse(input_json);
        if (j) {
            cJSON *f = cJSON_GetObjectItem(j, "freq_hz");
            if (cJSON_IsNumber(f)) {
                freq_hz = (uint32_t)f->valuedouble;
            }
            cJSON *d = cJSON_GetObjectItem(j, "duration_ms");
            if (cJSON_IsNumber(d) && d->valuedouble > 0 && d->valuedouble <= TONE_MAX_MS) {
                duration_ms = (uint32_t)d->valuedouble;
            }
            cJSON *v = cJSON_GetObjectItem(j, "volume");
            if (cJSON_IsNumber(v) && v->valuedouble > 0.0 && v->valuedouble <= TONE_MAX_VOL) {
                vol = (float)v->valuedouble;
            }
            cJSON_Delete(j);
        }
    }

    esp_err_t err = play_tone(freq_hz, duration_ms, vol);
    if (err == ESP_OK) {
        snprintf(output, output_size,
                 "Tone played: %lu Hz for %lu ms (volume %.2f) on MAX98357A (GPIO %d).",
                 (unsigned long)freq_hz, (unsigned long)duration_ms, vol, MIMI_GPIO_I2S_DOUT);
    } else if (err == ESP_ERR_INVALID_STATE) {
        snprintf(output, output_size, "Error: audio busy (recording or another tone playing).");
    } else if (err == ESP_ERR_INVALID_ARG) {
        snprintf(output, output_size,
                 "Error: freq_hz must be %d-%d, duration_ms 1-%d, volume 0.05-%.1f.",
                 TONE_MIN_FREQ_HZ, TONE_MAX_FREQ_HZ, TONE_MAX_MS, TONE_MAX_VOL);
    } else {
        snprintf(output, output_size, "Error: playback failed: %s", esp_err_to_name(err));
    }
    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/* audio_record_test — timed capture triggered by LLM / serial + STT test */
static esp_err_t audio_record_test_execute(const char *input_json, char *output, size_t output_size)
{
    uint32_t duration_ms = 4000;
    if (input_json) {
        cJSON *j = cJSON_Parse(input_json);
        if (j) {
            cJSON *d = cJSON_GetObjectItem(j, "duration_ms");
            if (cJSON_IsNumber(d) && d->valuedouble > 0 && d->valuedouble <= 10000) {
                duration_ms = (uint32_t)d->valuedouble;
            }
            cJSON_Delete(j);
        }
    }

    ESP_LOGI(TAG, "🎙️ [STT-TEST] Starting %lu ms recording — speak into mic now...", (unsigned long)duration_ms);
    tool_audio_beep(880, 150, 0.4f);   /* short cue beep */

    size_t   n  = 0;
    uint32_t dur = 0;
    double   rms = 0.0;
    int16_t  mn = 0, mx = 0;
    esp_err_t err = timed_capture(duration_ms, &n, &dur, &rms, &mn, &mx);
    if (err != ESP_OK) {
        snprintf(output, output_size,
                 "Error: capture failed to start (%s). Audio may be busy.",
                 esp_err_to_name(err));
        return err;
    }

    dump_capture_stats();

    ESP_LOGI(TAG, "🎙️ [STT-TEST] Sending audio to backend (/api/voice/transcribe)...");
    char *transcript = voice_client_transcribe(s_pcm_buf, n);
    if (transcript && strlen(transcript) > 0) {
        ESP_LOGI(TAG, "=================================================");
        ESP_LOGI(TAG, "🎉 [STT RESULT]: \"%s\"", transcript);
        ESP_LOGI(TAG, "=================================================");
        snprintf(output, output_size,
                 "{\"sample_count\":%u,\"duration_ms\":%u,\"rms\":%.1f,\"verdict\":\"%s\",\"transcript\":\"%s\"}",
                 (unsigned)n, (unsigned)dur, rms, mic_verdict(rms), transcript);
        free(transcript);
    } else {
        ESP_LOGW(TAG, "⚠️ [STT RESULT]: (no speech recognized or empty)");
        snprintf(output, output_size,
                 "{\"sample_count\":%u,\"duration_ms\":%u,\"rms\":%.1f,\"verdict\":\"%s\",\"transcript\":\"\"}",
                 (unsigned)n, (unsigned)dur, rms, mic_verdict(rms));
        if (transcript) free(transcript);
    }
    return ESP_OK;
}

/* =======================================================================
 * WAV playback — voice pipeline [PIPELINE-4]
 *
 * Plays a full WAV blob (typically fetched from the backend /api/voice/tts
 * endpoint, which returns 16 kHz mono 16-bit PCM). The header is parsed
 * with a proper RIFF chunk walk (some encoders insert extra chunks before
 * "data"). Frames are duplicated into both I2S slots like the tone path,
 * so any MAX98357A SD-pin channel select hears the audio.
 *
 * The backend guarantees 16 kHz; if a different rate ever appears we log
 * a warning and play anyway (pitch-shifted) rather than fail — the show
 * must go on, but the log tells you why it sounds funny.
 * ===================================================================== */

esp_err_t tool_audio_play_wav_turn(const uint8_t *wav, size_t len, uint32_t turn_gen)
{
    if (!s_i2s_initialized || !s_tx_handle || !s_tx_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!wav || len < 44) {
        return ESP_ERR_INVALID_ARG;
    }
    if (turn_gen != 0 && voice_client_is_turn_aborted(turn_gen)) {
        ESP_LOGW(TAG, "play_wav: skipping already-aborted turn gen=%lu", (unsigned long)turn_gen);
        return ESP_ERR_INVALID_STATE;
    }
    if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "play_wav: not a WAV blob (magic %c%c%c%c)",
                 wav[0], wav[1], wav[2], wav[3]);
        return ESP_ERR_INVALID_ARG;
    }

    /* --- RIFF chunk walk: find "fmt " and "data" --- */
    uint32_t sample_rate = 16000;
    uint16_t channels = 1, bits = 16;
    size_t data_off = 0, data_len = 0;
    size_t idx = 12;
    while (idx + 8 <= len) {
        uint32_t clen;
        memcpy(&clen, wav + idx + 4, 4);
        if (memcmp(wav + idx, "fmt ", 4) == 0 && idx + 24 <= len) {
            uint16_t audio_fmt;
            memcpy(&audio_fmt, wav + idx + 8, 2);
            memcpy(&channels,  wav + idx + 10, 2);
            memcpy(&sample_rate, wav + idx + 12, 4);
            memcpy(&bits,      wav + idx + 22, 2);
            if (audio_fmt != 1) {
                ESP_LOGE(TAG, "play_wav: unsupported WAV format %u (need PCM)", audio_fmt);
                return ESP_ERR_INVALID_ARG;
            }
        } else if (memcmp(wav + idx, "data", 4) == 0) {
            data_off = idx + 8;
            data_len = clen;
            if (data_off + data_len > len) {
                data_len = len - data_off;
            }
            break;
        }
        idx += 8 + clen + (clen & 1);   /* chunks are word-aligned */
    }
    if (data_off == 0 || data_len == 0) {
        ESP_LOGE(TAG, "play_wav: no data chunk found");
        return ESP_ERR_INVALID_ARG;
    }
    if (channels != 1 && channels != 2) {
        ESP_LOGE(TAG, "play_wav: unsupported channel count %u (need 1 or 2)", channels);
        return ESP_ERR_INVALID_ARG;
    }
    if (bits != 16) {
        ESP_LOGE(TAG, "play_wav: unsupported bit depth %u (need 16)", bits);
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_rate != AUDIO_SAMPLE_RATE) {
        ESP_LOGW(TAG, "play_wav: WAV is %lu Hz (expected %d) — playing pitch-shifted",
                 (unsigned long)sample_rate, AUDIO_SAMPLE_RATE);
    }

    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(AUDIO_TX_WAIT_WAV_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "play_wav: TX mutex busy (timeout %d ms)", AUDIO_TX_WAIT_WAV_MS);
        return ESP_ERR_INVALID_STATE;
    }

    if (turn_gen != 0 && voice_client_is_turn_aborted(turn_gen)) {
        xSemaphoreGive(s_tx_mutex);
        ESP_LOGW(TAG, "play_wav: turn gen=%lu aborted while waiting for TX", (unsigned long)turn_gen);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_recording) {
        xSemaphoreGive(s_tx_mutex);
        ESP_LOGW(TAG, "play_wav: busy (recording=1)");
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&s_audio_mux);
    s_playing = true;
    /* Gate the mic for the whole playback (feeder parks via s_speaker_busy).
     * voice_client wraps TTS in a wider session (download + play), but the
     * ack/beeps/other callers relied on this alone — ungated playback raced
     * the mic feeder (field: noise burst instead of speech). */
    audio_sync_speaker_busy_locked();
    taskEXIT_CRITICAL(&s_audio_mux);

    ESP_LOGI(TAG, "[SPK] WAV playback: data=%u bytes (~%lu ms), %lu Hz, %u ch, %u bit",
             (unsigned)data_len,
             (unsigned long)((uint64_t)data_len / (channels * 2) * 1000 / sample_rate),
             (unsigned long)sample_rate, channels, bits);
    ESP_LOGI(TAG, "[SPK] header check: RIFF ok, first data bytes %02X%02X %02X%02X %02X%02X %02X%02X",
             wav[data_off], wav[data_off+1], wav[data_off+2], wav[data_off+3],
             wav[data_off+4], wav[data_off+5], wav[data_off+6], wav[data_off+7]);

    /* --- stream frames: mono duplicated to both slots, stereo → left --- */
    int32_t *chunk = (int32_t *)malloc(AUDIO_TX_CHUNK_BYTES);
    if (!chunk) {
        taskENTER_CRITICAL(&s_audio_mux);
        s_playing = false;
        audio_sync_speaker_busy_locked();
        taskEXIT_CRITICAL(&s_audio_mux);
        xSemaphoreGive(s_tx_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t played = 0;
    bool aborted = false;
    esp_err_t err = ESP_OK;
    while (played < data_len) {
        if (turn_gen != 0 && voice_client_is_turn_aborted(turn_gen)) {
            ESP_LOGW(TAG, "[SPK] WAV playback interrupted (turn gen=%lu aborted)",
                     (unsigned long)turn_gen);
            aborted = true;
            err = ESP_ERR_INVALID_STATE;
            break;
        }

        size_t frames = AUDIO_TX_CHUNK_FRAMES;
        size_t need;
        if (channels == 2) {
            need = frames * 2 * sizeof(int16_t);          /* stereo source */
        } else {
            need = frames * sizeof(int16_t);              /* mono source */
        }
        if (need > data_len - played) {
            need = data_len - played;
            frames = (channels == 2) ? need / (2 * sizeof(int16_t))
                                     : need / sizeof(int16_t);
        }
        if (frames == 0) {
            break;
        }

        const int16_t *src = (const int16_t *)(wav + data_off + played);
        for (size_t f = 0; f < frames; f++) {
            int16_t sample = (channels == 2) ? src[f * 2] : src[f];
            int32_t v = (int32_t)((uint32_t)(uint16_t)sample << 16);
            chunk[f * 2]     = v;
            chunk[f * 2 + 1] = v;
        }

        size_t written = 0;
        err = i2s_channel_write(s_tx_handle, chunk,
                                frames * 2 * sizeof(int32_t), &written,
                                AUDIO_TX_WRITE_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "play_wav: i2s_channel_write FAILED: %s", esp_err_to_name(err));
            break;
        }
        played += (channels == 2) ? frames * 2 * sizeof(int16_t)
                                  : frames * sizeof(int16_t);
    }

    if (!aborted && err == ESP_OK) {
        audio_tx_drain_dma(chunk);
    }

    free(chunk);
    taskENTER_CRITICAL(&s_audio_mux);
    s_playing = false;
    audio_sync_speaker_busy_locked();
    taskEXIT_CRITICAL(&s_audio_mux);
    xSemaphoreGive(s_tx_mutex);
    ESP_LOGI(TAG, "[SPK] WAV done (%u bytes played)", (unsigned)played);
    return err;
}

esp_err_t tool_audio_play_wav(const uint8_t *wav, size_t len)
{
    return tool_audio_play_wav_turn(wav, len, 0);
}

/* Public beep helper — short feedback cue (record start / pipeline states).
 * Safe no-op when the bus is busy. */
void tool_audio_beep(uint32_t freq_hz, uint32_t duration_ms, float vol)
{
    play_tone_wait(freq_hz, duration_ms, vol, 0);
}

/* =======================================================================
 * Speaker diagnostic — audio_spktest (4 stages)
 *
 * Field history: TTS worked, then went silent across firmware changes.
 * This tool bisects the chain — run it, listen to each stage, and the
 * stage where sound stops is the broken link:
 *   1 fails                 → amp / speaker / wiring / power (SD→5V!)
 *   1 ok, 2 fails           → tool_audio_play_wav path (parser/format)
 *   1-2 ok, 3 soft inaudible→ audibility: TTS speech (~-24 dBFS) is quieter
 *                             than every ramp step; add playback gain
 *   1-3 ok, 4 silent        → server/network/pipeline (check `voice:` logs)
 * ===================================================================== */

#define SPKTEST_TONE_MS   1000
#define SPKTEST_RAMP_MS    700
#define SPKTEST_GAP_MS     250

/* Build a minimal 44-byte-header WAV (mono 16-bit @16 kHz) with a sine in
 * PSRAM — lets stage 2 exercise tool_audio_play_wav with zero network. */
static uint8_t *spktest_make_wav(uint32_t freq_hz, uint32_t ms, size_t *out_len)
{
    size_t n = (size_t)((uint64_t)AUDIO_SAMPLE_RATE * ms / 1000);
    size_t total = 44 + n * 2;
    uint8_t *buf = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return NULL;
    }
    uint32_t v32;
    uint16_t v16;
    memcpy(buf, "RIFF", 4);
    v32 = (uint32_t)(36 + n * 2);          memcpy(buf + 4,  &v32, 4);
    memcpy(buf + 8,  "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    v32 = 16;                              memcpy(buf + 16, &v32, 4);
    v16 = 1;                               memcpy(buf + 20, &v16, 2);   /* PCM    */
    v16 = 1;                               memcpy(buf + 22, &v16, 2);   /* mono   */
    v32 = AUDIO_SAMPLE_RATE;               memcpy(buf + 24, &v32, 4);
    v32 = AUDIO_SAMPLE_RATE * 2;           memcpy(buf + 28, &v32, 4);   /* Bps    */
    v16 = 2;                               memcpy(buf + 32, &v16, 2);   /* align  */
    v16 = 16;                              memcpy(buf + 34, &v16, 2);   /* bits   */
    memcpy(buf + 36, "data", 4);
    v32 = (uint32_t)(n * 2);               memcpy(buf + 40, &v32, 4);

    int16_t *pcm = (int16_t *)(buf + 44);
    double inc = 2.0 * M_PI * (double)freq_hz / AUDIO_SAMPLE_RATE;
    double ph = 0.0;
    for (size_t i = 0; i < n; i++) {
        pcm[i] = (int16_t)(sin(ph) * 20000.0);
        ph += inc;
        if (ph >= 2.0 * M_PI) ph -= 2.0 * M_PI;
    }
    *out_len = total;
    return buf;
}

static esp_err_t audio_spktest_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    if (!s_i2s_initialized) {
        snprintf(output, output_size, "Error: audio not initialized.");
        return ESP_FAIL;
    }
    if (s_recording || s_playing) {
        snprintf(output, output_size, "Error: audio busy (recording or playing).");
        return ESP_ERR_INVALID_STATE;
    }

    bool s1 = false, s2 = false, s4 = false;
    esp_err_t err;

    tool_audio_speaker_session_begin();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════ SPEAKER TEST — 4 STAGES ══════════╗");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

    /* ---- [1/4] hardware: raw tone ---- */
    ESP_LOGI(TAG, "[1/4] TONE 440 Hz, full volume, 1 s — expect a LOUD beep");
    err = play_tone(440, SPKTEST_TONE_MS, 1.0f);
    s1 = (err == ESP_OK);
    if (!s1) ESP_LOGE(TAG, "  stage 1 FAILED: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(SPKTEST_GAP_MS));

    /* ---- [2/4] local WAV through the parser/play path ---- */
    ESP_LOGI(TAG, "[2/4] LOCAL WAV 1 kHz (generated in PSRAM, no network) — expect the same beep");
    size_t wav_len = 0;
    uint8_t *wav = spktest_make_wav(1000, SPKTEST_TONE_MS, &wav_len);
    if (wav) {
        err = tool_audio_play_wav(wav, wav_len);
        s2 = (err == ESP_OK);
        free(wav);
        if (!s2) ESP_LOGE(TAG, "  stage 2 FAILED: %s", esp_err_to_name(err));
    } else {
        ESP_LOGE(TAG, "  stage 2 skipped: no memory for test WAV");
    }
    vTaskDelay(pdMS_TO_TICKS(SPKTEST_GAP_MS));

    /* ---- [3/4] volume ramp ---- */
    ESP_LOGI(TAG, "[3/4] VOLUME RAMP: 1 kHz at 1.0 / 0.4 / 0.15 — note where it becomes inaudible");
    static const float vols[3] = { 1.0f, 0.4f, 0.15f };
    for (int i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "  ramp %d/3: volume %.2f", i + 1, vols[i]);
        err = play_tone(1000, SPKTEST_RAMP_MS, vols[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "  ramp %d failed: %s", i + 1, esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(SPKTEST_GAP_MS));
    }

    /* ---- [4/4] full production pipeline (server TTS) ---- */
    ESP_LOGI(TAG, "[4/4] FULL PIPELINE: server TTS 'Speaker test, one, two, three'");
    s4 = voice_client_speak("Speaker test. One. Two. Three.");
    if (!s4) {
        ESP_LOGE(TAG, "  stage 4 could not start (pipeline busy) — see voice: logs");
    }

    tool_audio_speaker_session_end();

    /* ---- verdict guidance ---- */
    const char *verdict;
    if (!s1) {
        verdict = "HARDWARE: tone failed — check MAX98357A SD->5V, VIN->5V, DIN->GPIO21, speaker terminals";
    } else if (!s2) {
        verdict = "SOFTWARE: play_wav path failed — report [SPK] logs";
    } else if (!s4) {
        verdict = "PIPELINE: local playback OK but server TTS failed — check backend & WiFi";
    } else {
        verdict = "ALL STAGES PLAYED — compare loudness: if ramp was faint and speech silent, "
                  "speaker sensitivity is the issue (wiring/power), not the pipeline";
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════ SPEAKER TEST SUMMARY ══════════╗");
    ESP_LOGI(TAG, "║ 1/4 tone   : %s                     ║", s1 ? "played" : "FAILED");
    ESP_LOGI(TAG, "║ 2/4 wav    : %s                     ║", s2 ? "played" : "FAILED");
    ESP_LOGI(TAG, "║ 3/4 ramp   : played (note audibility) ║");
    ESP_LOGI(TAG, "║ 4/4 tts    : %s                     ║", s4 ? "played" : "FAILED");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "%s", verdict);

    snprintf(output, output_size,
             "{\"tone\":%s,\"local_wav\":%s,\"server_tts\":%s,"
             "\"verdict\":\"%s\"}",
             s1 ? "true" : "false", s2 ? "true" : "false", s4 ? "true" : "false",
             verdict);
    return ESP_OK;
}
