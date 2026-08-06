/*
 * tool_audio.c — I2S Microphone Capture (INMP441)
 *
 * Hardware (ESP32-S3, RX-only on I2S_NUM_0):
 *   GPIO 41 → BCLK  (INMP441 SCK  — clock driven by ESP32 as master)
 *   GPIO 42 → WS    (INMP441 WS   — word select / L/R clock)
 *   GPIO  1 ← DIN   (INMP441 SD   — mic serial data out → ESP32 RX)
 *   GPIO  2   DOUT  (MAX98357A DIN — not used, no speaker connected yet)
 *
 * NOTE: RX-only mode (NULL tx_handle) — no speaker, no full-duplex needed.
 *   Full-duplex mode required TX to be enabled for BCLK/WS to assert.
 *   RX-only is simpler and eliminates that dependency.
 *
 * INMP441 I2S format:
 *   - Philips I2S standard
 *   - 24-bit audio data, MSB-justified, in a 32-bit I2S slot
 *   - L/R pin = GND → data appears on LEFT channel
 *   - Layout: [LEFT_32bit][RIGHT_32bit] per stereo frame
 *   - Valid bits: bits 31..8 (upper 24 bits of the 32-bit word)
 *   - We read 32-bit words and right-shift 8 to get signed 24-bit,
 *     or right-shift 16 to get signed 16-bit PCM for speech.
 *
 * DMA buffer sizing:
 *   data_bit_width = 32BIT, slot_mode = STEREO
 *   → i2s_channel_read returns: (frames × 2 slots × 4 bytes) per read
 *   → chunk_bytes = chunk_frames × 2 × sizeof(int32_t)   [FIXED from 16BIT bug]
 *
 * Current mode: Hardware Verification
 *   Hold green button → capture PCM
 *   Release           → dump stats + hex to serial
 *
 * Future voice pipeline (TODO markers [PIPELINE-N]):
 *   [PIPELINE-1] POST PCM to /api/transcribe → transcript text
 *   [PIPELINE-2] Push transcript into agent_loop message bus
 *   [PIPELINE-3] Receive TTS WAV from /api/tts
 *   [PIPELINE-4] Play WAV via MAX98357A (add TX channel back, use full-duplex)
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
#include "cJSON.h"

#include <math.h>
#include <string.h>
#include <stdint.h>

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
 * One descriptor holds 512×8 = 4096 bytes ≈ 32 ms of audio.
 * Smaller dma_frame_num → lower latency, more CPU interrupts.
 * Larger → higher latency, fewer interrupts. 512 is a good balance. */
#define AUDIO_DMA_DESC_COUNT    4
#define AUDIO_DMA_FRAME_COUNT   512

/* Read chunk: how many frames we ask for per i2s_channel_read call.
 * Must be ≤ dma_frame_num. 256 frames = 16 ms at 16 kHz — responsive. */
#define AUDIO_READ_CHUNK_FRAMES 256
/* Stereo 32-bit: 2 slots × 4 bytes per frame */
#define AUDIO_READ_CHUNK_BYTES  (AUDIO_READ_CHUNK_FRAMES * 2 * sizeof(int32_t))

/* Read timeout: wait up to 500 ms for DMA to fill one chunk.
 * At 16 kHz, 256 frames = 16 ms. 500 ms is very generous — if we still
 * timeout, the I2S peripheral is definitely not clocking. */
#define AUDIO_READ_TIMEOUT_MS   500

/* Debug thresholds */
#define DUMP_SAMPLE_COUNT       64
#define SILENCE_RMS_THRESHOLD   80
#define AUDIO_RMS_THRESHOLD     500

/* -----------------------------------------------------------------------
 * I2S channel handle — RX only (no speaker)
 * --------------------------------------------------------------------- */
static i2s_chan_handle_t s_rx_handle   = NULL;
static bool              s_i2s_initialized = false;

/* -----------------------------------------------------------------------
 * Capture state
 * --------------------------------------------------------------------- */
static volatile bool s_recording          = false;
static TaskHandle_t  s_capture_task_handle = NULL;
static int16_t      *s_pcm_buf            = NULL; /* PSRAM — mono 16-bit */
static size_t        s_pcm_samples_captured = 0;
static uint32_t      s_record_start_ms    = 0;
static uint32_t      s_record_duration_ms = 0;

/* -----------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------- */
static esp_err_t audio_play_tone_execute(const char *in, char *out, size_t out_sz);
static esp_err_t audio_record_test_execute(const char *in, char *out, size_t out_sz);
static void      capture_task(void *arg);
static void      dump_capture_stats(void);
static void      log_gpio_states(void);

/* -----------------------------------------------------------------------
 * mimi_tool_t descriptors (extern in header)
 * --------------------------------------------------------------------- */
mimi_tool_t audio_play_tone_tool = {
    .name        = "audio_play_tone",
    .description = "Stub: speaker not connected. Returns an error message. "
                   "[PIPELINE-4] Wire TX channel back when MAX98357A speaker is added.",
    .input_schema_json = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
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

/* =======================================================================
 * GPIO Diagnostics
 *
 * Logs the level of every audio-related pin so we can spot wiring issues
 * without an oscilloscope. Called at init and before each capture.
 *
 * Expected idle states:
 *   GPIO  1 (DIN/SD)  — floating or LOW when no I2S clock. Irrelevant at idle.
 *   GPIO 41 (BCLK)    — LOW before I2S is enabled; toggles at ~1 MHz when active.
 *   GPIO 42 (WS)      — LOW before I2S is enabled; toggles at ~16 kHz when active.
 *   GPIO 39 (BTN)     — HIGH (pull-up, not pressed).
 * ===================================================================== */
static void log_gpio_states(void)
{
    int din  = gpio_get_level((gpio_num_t)MIMI_GPIO_I2S_DIN);   /* GPIO  1 */
    int dout = gpio_get_level((gpio_num_t)MIMI_GPIO_I2S_DOUT);  /* GPIO  2 */
    int bclk = gpio_get_level((gpio_num_t)MIMI_GPIO_I2S_BCLK);  /* GPIO 41 */
    int ws   = gpio_get_level((gpio_num_t)MIMI_GPIO_I2S_WS);    /* GPIO 42 */
    int btn  = gpio_get_level((gpio_num_t)MIMI_GPIO_BTN_GREEN);  /* GPIO 39 */

    ESP_LOGI(TAG, "+------ GPIO STATE SNAPSHOT -------------------------+");
    ESP_LOGI(TAG, "| GPIO  1 DIN  (INMP441 SD)   = %d  (0=LOW, 1=HIGH) |", din);
    ESP_LOGI(TAG, "| GPIO  2 DOUT (MAX98357A DIN) = %d  (unused)        |", dout);
    ESP_LOGI(TAG, "| GPIO 41 BCLK (shared clock)  = %d  (should be 0)   |", bclk);
    ESP_LOGI(TAG, "| GPIO 42 WS   (word select)   = %d  (should be 0)   |", ws);
    ESP_LOGI(TAG, "| GPIO 39 BTN  (green, active) = %d  (0=pressed)      |", btn);
    ESP_LOGI(TAG, "+-----------------------------------------------------+");

    /* Warn on suspicious pin states */
    if (bclk == 1) {
        ESP_LOGW(TAG, "  !! GPIO 41 (BCLK) is HIGH at idle — unexpected. "
                      "Check for conflict with another peripheral or driver.");
    }
    if (ws == 1) {
        ESP_LOGW(TAG, "  !! GPIO 42 (WS) is HIGH at idle — unexpected.");
    }
    if (din == 1) {
        ESP_LOGW(TAG, "  !! GPIO 1 (DIN) is HIGH at idle — INMP441 data line pulled high? "
                      "Check VCC and GND connections.");
    }
}

/* =======================================================================
 * I2S Initialization — RX-only (INMP441 capture, no TX)
 * ===================================================================== */

esp_err_t tool_audio_init(void)
{
    if (s_i2s_initialized) {
        ESP_LOGW(TAG, "I2S already initialized — skipping");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "=== tool_audio_init: INMP441 I2S RX-only ===");
    ESP_LOGI(TAG, "Pin map:");
    ESP_LOGI(TAG, "  BCLK = GPIO %d  (ESP32 drives this — INMP441 SCK)", MIMI_GPIO_I2S_BCLK);
    ESP_LOGI(TAG, "  WS   = GPIO %d  (ESP32 drives this — INMP441 WS)",  MIMI_GPIO_I2S_WS);
    ESP_LOGI(TAG, "  DIN  = GPIO %d  (INMP441 SD → ESP32 receives)",     MIMI_GPIO_I2S_DIN);
    ESP_LOGI(TAG, "  DOUT = GPIO %d  (unused — no speaker)",             MIMI_GPIO_I2S_DOUT);
    ESP_LOGI(TAG, "Config: %d Hz | 32-bit slot | stereo | Philips std",  AUDIO_SAMPLE_RATE);
    ESP_LOGI(TAG, "DMA: %d descriptors × %d frames (%d ms per desc)",
             AUDIO_DMA_DESC_COUNT, AUDIO_DMA_FRAME_COUNT,
             (AUDIO_DMA_FRAME_COUNT * 1000) / AUDIO_SAMPLE_RATE);

    /* Log pin states BEFORE touching I2S */
    log_gpio_states();

    /* ------------------------------------------------------------------
     * Step 1: Allocate RX-only channel.
     *
     * KEY FIX vs previous version: pass NULL for tx_handle.
     * Full-duplex (both TX+RX) required TX to be enabled for BCLK/WS
     * to assert on the shared bus. RX-only mode lets the ESP32 drive
     * BCLK+WS purely from the RX channel in master mode — no TX needed.
     * ------------------------------------------------------------------ */
    i2s_chan_config_t chan_cfg = {
        .id            = I2S_NUM_0,
        .role          = I2S_ROLE_MASTER,   /* ESP32 generates BCLK + WS */
        .dma_desc_num  = AUDIO_DMA_DESC_COUNT,
        .dma_frame_num = AUDIO_DMA_FRAME_COUNT,
        .auto_clear    = false,             /* RX: irrelevant */
    };

    ESP_LOGI(TAG, "[1/4] Allocating I2S_NUM_0 RX-only channel...");
    esp_err_t err = i2s_new_channel(&chan_cfg,
                                    NULL,          /* tx_handle = NULL → RX-only */
                                    &s_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel FAILED: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "  Possible causes: another driver already using I2S_NUM_0, "
                      "or insufficient memory.");
        return err;
    }
    ESP_LOGI(TAG, "[1/4] OK — RX handle: %p", (void *)s_rx_handle);

    /* ------------------------------------------------------------------
     * Step 2: Configure Philips I2S standard mode.
     *
     * KEY FIX vs previous version:
     *   Old: data_bit_width = 16BIT  ← BUG — DMA returns 2-byte samples,
     *        but we read as int32_t (4 bytes) → buffer never fills → timeout
     *   New: data_bit_width = 32BIT  ← CORRECT — DMA returns full 32-bit
     *        words matching the INMP441's 32-bit slot width.
     *
     * INMP441 slot layout (Philips, L/R=GND → left channel only):
     *   bit 31..8  = 24 valid audio bits (MSB first)
     *   bit  7..0  = always zero (padding)
     *   Frame: [LEFT_32b][RIGHT_32b] — right channel = 0x00000000
     *
     * Extraction: left_raw >> 8  → signed 24-bit
     *             left_raw >> 16 → signed 16-bit (what we store)
     * ------------------------------------------------------------------ */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT, /* FIX: was 16BIT */
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT, /* Philips slot = 32 */
            .slot_mode      = I2S_SLOT_MODE_STEREO,     /* INMP441 is a stereo I2S device */
            .slot_mask      = I2S_STD_SLOT_LEFT,        /* L/R=GND → data on left */
            .ws_width       = 32,                       /* WS high for 32 BCLK cycles */
            .ws_pol         = false,                    /* WS low = left channel */
            .bit_shift      = true,                     /* Philips: MSB 1 BCLK after WS edge */
            .left_align     = false,
            .big_endian     = false,
            .bit_order_lsb  = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIMI_GPIO_I2S_BCLK,   /* GPIO 41 — driven by ESP32 */
            .ws   = MIMI_GPIO_I2S_WS,     /* GPIO 42 — driven by ESP32 */
            .dout = I2S_GPIO_UNUSED,       /* No TX — was MIMI_GPIO_I2S_DOUT */
            .din  = MIMI_GPIO_I2S_DIN,    /* GPIO  1 — received from INMP441 */
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_LOGI(TAG, "[2/4] Configuring I2S STD Philips mode...");
    err = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode FAILED: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "  Possible causes: invalid GPIO numbers, pin conflict,");
        ESP_LOGE(TAG, "  or I2S peripheral not supported in this build config.");
        goto fail;
    }
    ESP_LOGI(TAG, "[2/4] OK — Philips 32-bit stereo configured");

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

    s_i2s_initialized = true;
    ESP_LOGI(TAG, "[4/4] tool_audio_init COMPLETE");
    ESP_LOGI(TAG, "  Hold green button (GPIO 39) to capture voice.");
    ESP_LOGI(TAG, "  Release to see amplitude stats in this console.");
    return ESP_OK;

fail:
    if (s_rx_handle) { i2s_del_channel(s_rx_handle); s_rx_handle = NULL; }
    if (s_pcm_buf)   { free(s_pcm_buf); s_pcm_buf = NULL; }
    return err;
}

/* =======================================================================
 * PTT Capture Task
 *
 * Called by xTaskCreate in tool_audio_ptt_record_start().
 * Runs on Core 1, priority 7 (above agent loop at 6).
 *
 * Read layout from i2s_channel_read (32-bit data, stereo):
 *   buf[0] = LEFT  channel frame 0  ← INMP441 audio (24-bit in 32-bit word)
 *   buf[1] = RIGHT channel frame 0  ← always 0 (INMP441 L/R=GND sends left only)
 *   buf[2] = LEFT  channel frame 1
 *   buf[3] = RIGHT channel frame 1
 *   ...
 *
 * Extraction to int16_t:
 *   int32_t raw = buf[f*2];          raw = 0xSSSSSS00  (S=sign bits, 00=padding)
 *   int16_t s16 = (int16_t)(raw >> 16);  → upper 16 bits of 24-bit value
 *
 * TODO [PIPELINE-1]: Replace dump_capture_stats() with HTTP POST to backend.
 * ===================================================================== */
static void capture_task(void *arg)
{
    /* Allocate read chunk on internal RAM (DMA accessible) */
    int32_t *dma_buf = (int32_t *)malloc(AUDIO_READ_CHUNK_BYTES);
    if (!dma_buf) {
        ESP_LOGE(TAG, "capture_task: chunk buffer malloc FAILED (%u bytes)",
                 (unsigned)AUDIO_READ_CHUNK_BYTES);
        s_recording = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "capture_task: starting — chunk=%u bytes, timeout=%d ms",
             (unsigned)AUDIO_READ_CHUNK_BYTES, AUDIO_READ_TIMEOUT_MS);
    log_gpio_states();  /* snapshot before enabling I2S */

    /* Enable RX — this asserts BCLK + WS on the GPIO pins */
    ESP_LOGI(TAG, "capture_task: enabling I2S RX (BCLK/WS will assert now)...");
    esp_err_t err = i2s_channel_enable(s_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "capture_task: i2s_channel_enable FAILED: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "  This means the I2S peripheral could not start clocking.");
        free(dma_buf);
        s_recording = false;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "capture_task: I2S RX enabled — BCLK should now toggle on GPIO %d",
             MIMI_GPIO_I2S_BCLK);

    /* Short delay to let BCLK stabilise and INMP441 wake up */
    vTaskDelay(pdMS_TO_TICKS(10));
    log_gpio_states();  /* snapshot after enabling — BCLK might read 1 or 0 mid-toggle */

    s_pcm_samples_captured = 0;
    s_record_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t read_count   = 0;
    uint32_t error_count  = 0;
    uint32_t timeout_count = 0;

    ESP_LOGI(TAG, "[MIC] Recording started — max %d s, buf %u KB",
             AUDIO_MAX_RECORD_MS / 1000,
             (unsigned)(AUDIO_MAX_SAMPLES * sizeof(int16_t) / 1024));

    while (s_recording && s_pcm_samples_captured < AUDIO_MAX_SAMPLES) {
        size_t bytes_read = 0;
        err = i2s_channel_read(s_rx_handle, dma_buf,
                               AUDIO_READ_CHUNK_BYTES, &bytes_read,
                               pdMS_TO_TICKS(AUDIO_READ_TIMEOUT_MS));

        read_count++;

        if (err == ESP_ERR_TIMEOUT) {
            timeout_count++;
            ESP_LOGW(TAG, "  [read #%u] TIMEOUT after %d ms — DMA empty.",
                     (unsigned)read_count, AUDIO_READ_TIMEOUT_MS);
            ESP_LOGW(TAG, "  Possible causes:");
            ESP_LOGW(TAG, "    (a) BCLK/WS not reaching INMP441 — check wiring GPIO 41,42");
            ESP_LOGW(TAG, "    (b) INMP441 DIN pin (GPIO 1) not connected to INMP441 SD");
            ESP_LOGW(TAG, "    (c) INMP441 VCC not at 3.3V (check power)");
            ESP_LOGW(TAG, "    (d) INMP441 L/R pin not tied to GND (check wiring)");
            if (timeout_count >= 3) {
                ESP_LOGE(TAG, "  !! %u consecutive timeouts — I2S clock almost certainly not "
                              "reaching INMP441. Stopping capture.", (unsigned)timeout_count);
                /* Dump GPIO snapshot to help diagnose */
                log_gpio_states();
                break;
            }
            continue;
        } else if (err != ESP_OK) {
            error_count++;
            ESP_LOGW(TAG, "  [read #%u] ERROR: %s (bytes_read=%u)",
                     (unsigned)read_count, esp_err_to_name(err), (unsigned)bytes_read);
            if (error_count > 10) {
                ESP_LOGE(TAG, "  !! Too many errors — aborting capture.");
                break;
            }
            continue;
        }

        /* Reset consecutive timeout counter on successful read */
        timeout_count = 0;

        /* Convert stereo 32-bit frames → mono int16_t
         * buf layout: [L0][R0][L1][R1]...
         * L = INMP441 data (24-bit MSB-justified in 32-bit word)
         * R = 0x00000000 (INMP441 L/R=GND outputs left only)
         * int16 = raw >> 16  (upper 16 of 24 valid bits) */
        size_t frames_read = bytes_read / (2 * sizeof(int32_t));
        for (size_t f = 0; f < frames_read && s_pcm_samples_captured < AUDIO_MAX_SAMPLES; f++) {
            int32_t raw_left = dma_buf[f * 2];
            s_pcm_buf[s_pcm_samples_captured++] = (int16_t)(raw_left >> 16);
        }

        /* Progress log every ~1 second */
        if ((read_count % (AUDIO_SAMPLE_RATE / AUDIO_READ_CHUNK_FRAMES)) == 0) {
            ESP_LOGI(TAG, "  [record] %u ms | %u samples | read#%u ok",
                     (unsigned)(s_pcm_samples_captured * 1000 / AUDIO_SAMPLE_RATE),
                     (unsigned)s_pcm_samples_captured,
                     (unsigned)read_count);

            /* Log first 4 raw int32 values from this chunk for sanity */
            ESP_LOGI(TAG, "  [raw] L0=0x%08X L1=0x%08X L2=0x%08X L3=0x%08X",
                     (unsigned)dma_buf[0], (unsigned)dma_buf[2],
                     (unsigned)dma_buf[4], (unsigned)dma_buf[6]);
        }
    }

    s_record_duration_ms = (uint32_t)(esp_timer_get_time() / 1000) - s_record_start_ms;

    ESP_LOGI(TAG, "capture_task: disabling I2S RX...");
    i2s_channel_disable(s_rx_handle);

    free(dma_buf);

    ESP_LOGI(TAG, "[MIC] Recording stopped — %u samples | %u ms | %u reads | %u errors",
             (unsigned)s_pcm_samples_captured, (unsigned)s_record_duration_ms,
             (unsigned)read_count, (unsigned)error_count);

    s_capture_task_handle = NULL;
    vTaskDelete(NULL);
}

/* =======================================================================
 * PTT Public API
 * ===================================================================== */

void tool_audio_ptt_record_start(void)
{
    if (!s_i2s_initialized) {
        ESP_LOGE(TAG, "ptt_record_start: not initialized — call tool_audio_init() first");
        return;
    }
    if (s_recording) {
        ESP_LOGW(TAG, "ptt_record_start: already recording — ignored");
        return;
    }

    ESP_LOGI(TAG, "[PTT] Green button pressed — starting capture...");
    s_recording = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        capture_task, "audio_cap", 4096, NULL,
        7,   /* prio 7: above agent loop (6) so DMA reads are never starved */
        &s_capture_task_handle,
        1);  /* Core 1: keep audio off WiFi core */

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "ptt_record_start: xTaskCreate FAILED — out of task slots or stack?");
        s_recording = false;
    }
}

void tool_audio_ptt_record_stop(void)
{
    if (!s_recording && s_capture_task_handle == NULL) {
        ESP_LOGW(TAG, "ptt_record_stop: not recording — ignored");
        return;
    }

    ESP_LOGI(TAG, "[PTT] Green button released — stopping capture...");
    s_recording = false;

    /* Wait for capture task to self-delete (max 1.5 s) */
    uint32_t waited_ms = 0;
    while (s_capture_task_handle != NULL && waited_ms < 1500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited_ms += 10;
    }
    if (s_capture_task_handle != NULL) {
        ESP_LOGW(TAG, "ptt_record_stop: task did not exit — force killing");
        vTaskDelete(s_capture_task_handle);
        s_capture_task_handle = NULL;
        i2s_channel_disable(s_rx_handle);
    }

    dump_capture_stats();

    /* TODO [PIPELINE-1]: POST s_pcm_buf to backend transcription endpoint:
     *   if (s_pcm_samples_captured > 0) {
     *       audio_pipeline_post_to_backend(s_pcm_buf, s_pcm_samples_captured,
     *                                      AUDIO_SAMPLE_RATE, s_record_duration_ms);
     *   }
     */
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

/* =======================================================================
 * Serial Diagnostic Dump — called on button release
 * ===================================================================== */

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
        ESP_LOGE(TAG, "  [ ] INMP441 SCK ← GPIO 41 (BCLK from ESP32)");
        ESP_LOGE(TAG, "  [ ] INMP441 WS  ← GPIO 42 (WS from ESP32)");
        ESP_LOGE(TAG, "  [ ] INMP441 SD  → GPIO 1  (data TO ESP32)");
        ESP_LOGE(TAG, "  [ ] GPIO 41 and 42 shared with nothing else");
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
 * mimi_tool_t Execute Stubs
 * ===================================================================== */

/* audio_play_tone — stub, no speaker connected */
static esp_err_t audio_play_tone_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    /* TODO [PIPELINE-4]: Re-add TX channel, generate sine, write to I2S TX */
    snprintf(output, output_size,
             "No speaker connected (MAX98357A TX not configured). "
             "[PIPELINE-4] Add TX channel back when speaker is wired.");
    ESP_LOGW(TAG, "audio_play_tone called but no speaker connected");
    return ESP_OK;
}

/* audio_record_test — timed capture triggered by LLM */
static esp_err_t audio_record_test_execute(const char *input_json, char *output, size_t output_size)
{
    uint32_t duration_ms = 3000;
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

    if (!s_i2s_initialized) {
        snprintf(output, output_size, "Error: audio not initialized.");
        return ESP_FAIL;
    }
    if (s_recording) {
        snprintf(output, output_size, "Error: PTT recording already in progress.");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "[MIC-TEST] LLM triggered %u ms timed recording", (unsigned)duration_ms);

    s_recording = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        capture_task, "audio_cap_t", 4096, NULL, 7, &s_capture_task_handle, 1);
    if (ret != pdPASS) {
        s_recording = false;
        snprintf(output, output_size, "Error: failed to start capture task.");
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    s_recording = false;

    uint32_t wait_ms = 0;
    while (s_capture_task_handle != NULL && wait_ms < 1500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_ms += 10;
    }

    size_t   n   = s_pcm_samples_captured;
    uint32_t dur = s_record_duration_ms;
    int16_t  mn  = INT16_MAX, mx = INT16_MIN;
    int64_t  ssq = 0;
    for (size_t i = 0; i < n; i++) {
        int16_t s = s_pcm_buf[i];
        if (s < mn) mn = s;
        if (s > mx) mx = s;
        ssq += (int64_t)s * s;
    }
    double rms = (n > 0) ? sqrt((double)ssq / (double)n) : 0.0;

    snprintf(output, output_size,
             "{\"sample_count\":%u,\"duration_ms\":%u,\"sample_rate\":%d,"
             "\"rms\":%.1f,\"min\":%d,\"max\":%d,\"verdict\":\"%s\"}",
             (unsigned)n, (unsigned)dur, AUDIO_SAMPLE_RATE,
             rms, (int)mn, (int)mx,
             rms < SILENCE_RMS_THRESHOLD ? "silence" :
             rms < AUDIO_RMS_THRESHOLD   ? "low_signal" : "audio_detected");

    dump_capture_stats();
    /* TODO [PIPELINE-1]: POST s_pcm_buf to backend here */
    return ESP_OK;
}
