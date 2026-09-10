/*
 * voice_client.c — hands-free voice pipeline (esp-sr WakeNet + backend STT/TTS)
 *
 * Flow (no button — the mic is always the entry point):
 *
 *   LISTEN   WakeNet ("Hi ESP" model, WN9) consumes the I2S RX stream.
 *   WAKE     → "Yes?" ack (pre-cached TTS WAV in PSRAM, plays instantly)
 *            → command window opens: capture until ~1.6 s silence or 10 s.
 *   THINK    → POST PCM /api/voice/transcribe → transcript
 *            → inbound bus as system:"voice" message → agent ReAct loop
 *   SPEAK    → agent reply routed back by mimi.c → voice_client_speak()
 *            → POST /api/voice/tts → WAV → MAX98357A.
 *
 * The wake engine and the speaker share the bus: the listener pauses while
 * anything plays or records (tool_audio_is_busy), so the device never
 * hears itself.
 */

#include "voice/voice_client.h"
#include "mimi_config.h"
#include "tools/tool_audio.h"
#include "bus/message_bus.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"   /* esp_srmodel_init / esp_srmodel_filter */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

/* debug: feeder-side DMA counters (defined in tool_audio.c) */
extern volatile uint32_t s_feeder_reads;
extern volatile uint32_t s_feeder_zeros;
extern volatile uint32_t s_ring_written;

static const char *TAG = "voice";

/* -----------------------------------------------------------------------
 * Tuning
 * --------------------------------------------------------------------- */
#define VOICE_TASK_STACK       (12 * 1024)  /* stack lives in PSRAM (see start) */
#define VOICE_TASK_PRIO        3           /* MUST stay low: the wake loop is CPU-hungry
                                               (continuous I2S reads + WakeNet); at prio 5 it
                                               starved IDLE0 (field WDT spam) and TLS/AES */
#define VOICE_TASK_CORE        0           /* networking core; WakeNet is light */

#define WAKE_CHUNK_MS          30          /* feed WakeNet in 30 ms frames */
#define WAKE_CHUNK_SAMPLES     (TOOL_AUDIO_SAMPLE_RATE * WAKE_CHUNK_MS / 1000)

#define ACK_CACHE_BYTES        (96 * 1024) /* "Yes?" WAV cache in PSRAM */

#define CMD_MAX_MS             10000       /* hard cap on command capture */
#define CMD_MAX_SAMPLES        (TOOL_AUDIO_SAMPLE_RATE * CMD_MAX_MS / 1000)
#define CMD_SILENCE_MS         1600        /* stop after this much silence */
#define CMD_SILENCE_RMS        350         /* quiet-room floor threshold */

#define VOICE_HTTP_TIMEOUT_MS  30000
#define VOICE_HTTP_RX_MAX      (2 * 1024 * 1024)  /* PSRAM; long replies hit the
                                                   old 256 KB cap (field: a 194-char
                                                   reply made a 262 KB WAV -> oom=1) */
#define VOICE_TX_CHUNK         (4 * 1024)

#define BEEP_ACK_FALLBACK_HZ   880
#define BEEP_ERR_HZ            220
#define BEEP_TIMEOUT_HZ        330
#define BEEP_MS                120
#define BEEP_VOL               0.4f

/* -----------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------- */
static srmodel_list_t      *s_srmodels       = NULL;
static char                *s_wn_name        = NULL;   /* e.g. "wn9_hiesp" */
static const esp_wn_iface_t *s_wn_iface      = NULL;
static model_iface_data_t  *s_wn_data        = NULL;
static int                  s_wn_chunk       = 512;    /* samples per detect() */
static int16_t             *s_wn_buf         = NULL;   /* accumulation buffer */
static TaskHandle_t         s_voice_task     = NULL;
static StackType_t         *s_voice_stack    = NULL;  /* PSRAM-allocated */
static StaticTask_t        *s_voice_tcb      = NULL;  /* internal RAM */
static volatile bool        s_running        = false;
static volatile bool        s_busy           = false;  /* pipeline in flight */
static uint8_t             *s_ack_wav        = NULL;   /* "Yes?" cache */
static size_t               s_ack_wav_len    = 0;

/* -----------------------------------------------------------------------
 * HTTP helpers (PSRAM buffers)
 * --------------------------------------------------------------------- */

static char *voice_backend_url(const char *path)
{
    static char url[128];
    snprintf(url, sizeof(url), "%s%s", MIMI_BACKEND_URL, path);
    return url;
}

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
    bool oom;
} rx_buf_t;

static esp_err_t rx_event_handler(esp_http_client_event_t *evt)
{
    rx_buf_t *rx = (rx_buf_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || rx == NULL || evt->data_len == 0) {
        return ESP_OK;
    }
    if (rx->len + evt->data_len > rx->cap) {
        rx->oom = true;
        return ESP_OK;
    }
    memcpy(rx->buf + rx->len, evt->data, evt->data_len);
    rx->len += evt->data_len;
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * Ack: fetch "Yes?" once, keep in PSRAM
 * --------------------------------------------------------------------- */

static bool ack_fetch(void)
{
    s_ack_wav = heap_caps_calloc(1, ACK_CACHE_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_ack_wav) {
        return false;
    }

    tool_audio_speaker_session_begin();   /* feeder parks for download too */
    bool ok = false;
    char *body = "{\"text\":\"Yes?\"}";
    rx_buf_t rx = { .buf = s_ack_wav, .len = 0, .cap = ACK_CACHE_BYTES, .oom = false };

    esp_http_client_config_t cfg = {
        .url = voice_backend_url("/api/voice/tts"),
        .method = HTTP_METHOD_POST,
        .timeout_ms = VOICE_HTTP_TIMEOUT_MS,
        .event_handler = rx_event_handler,
        .user_data = &rx,
        .buffer_size = VOICE_TX_CHUNK,
        .buffer_size_tx = VOICE_TX_CHUNK,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return false;
    }
    esp_http_client_set_header(client, "Authorization", "Bearer " MIMI_BACKEND_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200 && !rx.oom && rx.len > 44) {
        s_ack_wav_len = rx.len;
        ESP_LOGI(TAG, "ack WAV cached (%u bytes)", (unsigned)s_ack_wav_len);
        ok = true;
    } else {
        ESP_LOGW(TAG, "ack fetch failed (err=%s status=%d) — will beep instead",
                 esp_err_to_name(err), status);
        free(s_ack_wav);
        s_ack_wav = NULL;
    }
    tool_audio_speaker_session_end();
    return ok;
}

static void ack_play(void)
{
    if (s_ack_wav) {
        tool_audio_play_wav(s_ack_wav, s_ack_wav_len);
    } else {
        tool_audio_beep(BEEP_ACK_FALLBACK_HZ, BEEP_MS, BEEP_VOL);
    }
}

/* -----------------------------------------------------------------------
 * STT: POST raw PCM → {"text": ...}
 * --------------------------------------------------------------------- */

static char *voice_transcribe(const int16_t *pcm, size_t samples)
{
    size_t body_len = samples * sizeof(int16_t);
    rx_buf_t *rx = heap_caps_calloc(1, sizeof(rx_buf_t), MALLOC_CAP_SPIRAM);
    rx->cap = 8 * 1024;
    rx->buf = heap_caps_calloc(1, rx->cap, MALLOC_CAP_SPIRAM);
    if (!rx || !rx->buf) {
        free(rx);
        return NULL;
    }

    esp_http_client_config_t cfg = {
        .url = voice_backend_url("/api/voice/transcribe"),
        .method = HTTP_METHOD_POST,
        .timeout_ms = VOICE_HTTP_TIMEOUT_MS,
        .event_handler = rx_event_handler,
        .user_data = rx,
        .buffer_size = VOICE_TX_CHUNK,
        .buffer_size_tx = VOICE_TX_CHUNK,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(rx->buf);
        free(rx);
        return NULL;
    }
    esp_http_client_set_header(client, "Authorization", "Bearer " MIMI_BACKEND_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(client, (const char *)pcm, (int)body_len);

    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    uint32_t dt = (uint32_t)(esp_timer_get_time() / 1000) - t0;

    char *text = NULL;
    if (err == ESP_OK && status == 200 && !rx->oom && rx->len > 0) {
        rx->buf[rx->len < rx->cap - 1 ? rx->len : rx->cap - 1] = '\0';
        cJSON *j = cJSON_Parse((const char *)rx->buf);
        cJSON *t = j ? cJSON_GetObjectItem(j, "text") : NULL;
        if (cJSON_IsString(t) && t->valuestring[0] != '\0') {
            text = strdup(t->valuestring);
        }
        cJSON_Delete(j);
        ESP_LOGI(TAG, "STT %u ms, %u -> %u bytes: %.60s",
                 (unsigned)dt, (unsigned)body_len, (unsigned)rx->len, text ? text : "(empty)");
    } else {
        ESP_LOGE(TAG, "STT FAILED: err=%s status=%d oom=%d len=%u (%u ms)",
                 esp_err_to_name(err), status, rx->oom, (unsigned)rx->len, (unsigned)dt);
    }

    free(rx->buf);
    free(rx);
    return text;
}

/* -----------------------------------------------------------------------
 * TTS speak: text → WAV → speaker
 * --------------------------------------------------------------------- */

static uint8_t *voice_tts_fetch(const char *text, size_t *out_len)
{
    char *body = NULL;
    cJSON *j = cJSON_CreateObject();
    if (!j) return NULL;
    cJSON_AddStringToObject(j, "text", text);
    body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body) return NULL;

    rx_buf_t *rx = heap_caps_calloc(1, sizeof(rx_buf_t), MALLOC_CAP_SPIRAM);
    rx->cap = VOICE_HTTP_RX_MAX;
    rx->buf = heap_caps_calloc(1, rx->cap, MALLOC_CAP_SPIRAM);
    if (!rx || !rx->buf) {
        free(body);
        free(rx);
        return NULL;
    }

    esp_http_client_config_t cfg = {
        .url = voice_backend_url("/api/voice/tts"),
        .method = HTTP_METHOD_POST,
        .timeout_ms = VOICE_HTTP_TIMEOUT_MS,
        .event_handler = rx_event_handler,
        .user_data = rx,
        .buffer_size = VOICE_TX_CHUNK,
        .buffer_size_tx = VOICE_TX_CHUNK,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(rx->buf);
        free(rx);
        free(body);
        return NULL;
    }
    esp_http_client_set_header(client, "Authorization", "Bearer " MIMI_BACKEND_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    uint32_t dt = (uint32_t)(esp_timer_get_time() / 1000) - t0;
    free(body);

    uint8_t *wav = NULL;
    if (err == ESP_OK && status == 200 && !rx->oom && rx->len > 44) {
        *out_len = rx->len;
        wav = rx->buf;
        ESP_LOGI(TAG, "TTS %u ms, %u bytes WAV", (unsigned)dt, (unsigned)rx->len);
    } else {
        ESP_LOGE(TAG, "TTS FAILED: err=%s status=%d oom=%d len=%u (%u ms)",
                 esp_err_to_name(err), status, rx->oom, (unsigned)rx->len, (unsigned)dt);
        free(rx->buf);
        free(rx);
    }
    return wav;
}

static void voice_speak_internal(const char *text)
{
    /* Whole session gated: RX feeder parks for download AND playback.
     * (Gating only playback left the download racing the feeder and TX
     * audio came out as noise — field: "ESPeting..." sounded like static.) */
    tool_audio_speaker_session_begin();
    size_t wav_len = 0;
    uint8_t *wav = voice_tts_fetch(text, &wav_len);
    if (!wav) {
        tool_audio_speaker_session_end();
        tool_audio_beep(BEEP_ERR_HZ, BEEP_MS * 2, BEEP_VOL);
        return;
    }
    esp_err_t perr = tool_audio_play_wav(wav, wav_len);
    free(wav);
    tool_audio_speaker_session_end();
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "WAV playback failed: %s", esp_err_to_name(perr));
    }
}

/* -----------------------------------------------------------------------
 * Command phase: capture → STT → agent bus
 * --------------------------------------------------------------------- */

static void voice_run_command(void)
{
    if (!tool_audio_capture_arm(CMD_MAX_SAMPLES)) {
        ESP_LOGW(TAG, "command capture could not arm (bus busy)");
        return;
    }

    ESP_LOGI(TAG, "[CMD] Listening for your command (silence stops after %d ms)...",
             CMD_SILENCE_MS);

    int16_t scratch[WAKE_CHUNK_SAMPLES];
    size_t total = 0;
    int64_t last_voice_ms = esp_timer_get_time() / 1000;
    int64_t start_ms = last_voice_ms;

    bool capturing = true;
    while (capturing) {
        size_t written = 0;
        bool full = false;
        capturing = tool_audio_rx_read(scratch, WAKE_CHUNK_SAMPLES, &written,
                                       &total, &full, WAKE_CHUNK_MS);

        if (written > 0) {
            /* track speech activity on the scratch chunk */
            int64_t ssq = 0;
            for (size_t i = 0; i < written; i++) {
                ssq += (int64_t)scratch[i] * scratch[i];
            }
            double rms = sqrt((double)ssq / (double)written);
            if (rms > CMD_SILENCE_RMS) {
                last_voice_ms = esp_timer_get_time() / 1000;
            }
        }
        if (full) {
            break;
        }
        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_voice_ms >= CMD_SILENCE_MS) {
            ESP_LOGI(TAG, "[CMD] silence timeout");
            break;
        }
        if (now - start_ms >= CMD_MAX_MS) {
            ESP_LOGI(TAG, "[CMD] max duration reached");
            break;
        }
    }

    tool_audio_capture_disarm();
    uint32_t dur = (uint32_t)(esp_timer_get_time() / 1000) - (uint32_t)start_ms;
    ESP_LOGI(TAG, "[CMD] captured %u samples (%u ms)", (unsigned)total, (unsigned)dur);

    if (total < TOOL_AUDIO_SAMPLE_RATE / 2) {
        ESP_LOGW(TAG, "command too short — nothing to ask");
        tool_audio_beep(BEEP_TIMEOUT_HZ, BEEP_MS, BEEP_VOL);
        return;
    }

    size_t samples = 0;
    const int16_t *pcm = tool_audio_get_last_capture(&samples, &dur);
    if (!pcm) {
        return;
    }

    ESP_LOGI(TAG, "=== voice pipeline: STT (%u samples / %u ms) ===",
             (unsigned)samples, (unsigned)dur);

    char *text = voice_transcribe(pcm, samples);
    if (!text) {
        tool_audio_beep(BEEP_ERR_HZ, BEEP_MS * 2, BEEP_VOL);
        return;
    }
    if (strlen(text) < 1) {
        ESP_LOGW(TAG, "transcript empty — nothing to ask");
        free(text);
        tool_audio_beep(BEEP_TIMEOUT_HZ, BEEP_MS, BEEP_VOL);
        return;
    }

    /* Transcript → agent loop as a system:"voice" message. */
    mimi_msg_t msg = {0};
    strlcpy(msg.channel, MIMI_CHAN_SYSTEM, sizeof(msg.channel));
    strlcpy(msg.chat_id, "voice", sizeof(msg.chat_id));
    msg.content = text;                    /* bus takes ownership */
    esp_err_t perr = message_bus_push_inbound(&msg);
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "inbound push failed: %s", esp_err_to_name(perr));
        free(text);
    } else {
        ESP_LOGI(TAG, "transcript queued to agent: %.60s", text);
    }
}

/* -----------------------------------------------------------------------
 * Wake listener task
 * --------------------------------------------------------------------- */

static void voice_task(void *arg)
{
    /* NOTE: sized for s_wn_chunk (512) — the ring read fills up to a full
     * wakenet frame; WAKE_CHUNK_SAMPLES (480) is only the per-read slice. */
    int16_t chunk[512];
    size_t written = 0;

    /* ---- listen diagnostics (rate-limited) ---- */
    uint32_t dbg_frames   = 0;      /* wakenet frames processed */
    uint64_t dbg_rms_sum  = 0;      /* sum of per-frame RMS */
    int32_t  dbg_rms_peak = 0;      /* loudest frame since last report */
    uint32_t dbg_loud     = 0;      /* frames above speech level */
    uint32_t dbg_empties  = 0;      /* paused/empty iterations */
    int64_t  dbg_last_report_ms = esp_timer_get_time() / 1000;
    int64_t  dbg_last_loud_log = 0;
    bool     was_paused = false;

    ESP_LOGI(TAG, "wake listener running — say \"Hi ESP\" (debug stats every 5 s)");

    while (s_running) {
        /* Pause while the speaker plays or another capture runs — the
         * device must not hear itself. */
        if (s_busy || tool_audio_is_busy()) {
            dbg_empties++;
            was_paused = true;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (was_paused) {
            was_paused = false;
            tool_audio_ring_flush();   /* drop stale audio buffered while paused */
        }

        if (!s_wn_iface || !s_wn_data) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* Read the mic from the ring buffer STRAIGHT INTO s_wn_buf —
         * detect() consumes s_wn_buf. (An earlier refactor read into
         * `chunk` and never filled s_wn_buf: the model was fed calloc'd
         * zeros — field rms=0, no detection.) The prio-7 mic_feed task
         * owns the I2S DMA; the ring keeps the stream gap-free. */
        size_t filled = 0;
        while (filled < (size_t)s_wn_chunk) {
            size_t got = tool_audio_ring_read(s_wn_buf + filled,
                                              (size_t)s_wn_chunk - filled,
                                              WAKE_CHUNK_MS);
            if (got == 0) {
                dbg_empties++;
                break;
            }
            filled += got;
        }
        if (filled < (size_t)s_wn_chunk) {
            vTaskDelay(1);   /* no full frame yet — yield instead of spinning */
            continue;
        }

        /* Yield before inference: without this the read-then-detect loop runs
         * flat out and starves IDLE0 (task watchdog fired every 5 s in the
         * field) and every CPU-0 network task. One tick per 32 ms of audio
         * still consumes faster than the mic produces. */
        vTaskDelay(1);

        /* ---- per-frame debug stats ---- */
        {
            int64_t ssq = 0;
            for (int i = 0; i < s_wn_chunk; i++) {
                ssq += (int64_t)s_wn_buf[i] * s_wn_buf[i];
            }
            int32_t rms = (int32_t)sqrt((double)ssq / (double)s_wn_chunk);
            dbg_frames++;
            dbg_rms_sum += rms;
            if (rms > dbg_rms_peak) dbg_rms_peak = rms;
            if (rms > 1500) {
                dbg_loud++;
                int64_t now = esp_timer_get_time() / 1000;
                if (now - dbg_last_loud_log > 1000) {
                    dbg_last_loud_log = now;
                    ESP_LOGI(TAG, "[listen] voice-level audio: rms=%d (mic hears you)", rms);
                }
            }
        }

        wakenet_state_t state = s_wn_iface->detect(s_wn_data, s_wn_buf);
        if (state != WAKENET_NO_DETECT && state != WAKENET_DETECTED) {
            ESP_LOGI(TAG, "[listen] wakenet intermediate state=%d", state);
        }
        if (state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "*** WAKE WORD DETECTED ***");
            s_busy = true;

            ack_play();
            vTaskDelay(pdMS_TO_TICKS(150));   /* let the ack finish cleanly */

            voice_run_command();

            s_busy = false;
            ESP_LOGI(TAG, "pipeline done — listening for \"Hi ESP\" again");
        }

        /* ---- 5-second debug report ---- */
        {
            int64_t now = esp_timer_get_time() / 1000;
            if (now - dbg_last_report_ms >= 5000) {
                uint32_t avg = dbg_frames ? (uint32_t)(dbg_rms_sum / dbg_frames) : 0;
                ESP_LOGI(TAG, "[listen] 5s: frames=%u avg_rms=%u peak_rms=%d loud=%u paused/empty=%u",
                         (unsigned)dbg_frames, avg, (int)dbg_rms_peak,
                         (unsigned)dbg_loud, (unsigned)dbg_empties);
                ESP_LOGI(TAG, "[debug] feeder: dma_reads=%u zero_reads=%u ring_written=%u | frame[0..3]=%d %d %d %d",
                         (unsigned)s_feeder_reads, (unsigned)s_feeder_zeros,
                         (unsigned)s_ring_written,
                         (int)s_wn_buf[0], (int)s_wn_buf[1],
                         (int)s_wn_buf[2], (int)s_wn_buf[3]);
                if (dbg_frames == 0) {
                    ESP_LOGW(TAG, "[listen] NO AUDIO FRAMES — mic stream not reaching WakeNet!");
                } else if (dbg_rms_peak < 800) {
                    ESP_LOGW(TAG, "[listen] audio flows but quiet (peak %d) — speak closer/louder",
                             (int)dbg_rms_peak);
                }
                dbg_frames = 0; dbg_rms_sum = 0; dbg_rms_peak = 0; dbg_loud = 0; dbg_empties = 0;
                dbg_last_report_ms = now;
            }
        }
    }
    ESP_LOGI(TAG, "wake listener stopped");
    s_voice_task = NULL;
    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

bool voice_client_speak(const char *text)
{
    if (!text || !*text) {
        return false;
    }
    if (s_busy) {
        ESP_LOGW(TAG, "pipeline busy — dropping speak request (%.40s)", text);
        return false;
    }
    s_busy = true;
    voice_speak_internal(text);
    s_busy = false;
    return true;
}

/* Retained for compatibility: PTT-style trigger now just opens the command
 * window directly (wake word bypassed). Not wired to any button. */
void voice_client_on_ptt_stop(void)
{
    if (s_busy) {
        return;
    }
    s_busy = true;
    ack_play();
    vTaskDelay(pdMS_TO_TICKS(150));
    voice_run_command();
    s_busy = false;
}

esp_err_t voice_client_start(void)
{
    if (s_voice_task) {
        return ESP_OK;
    }

    /* WakeNet: "Hi ESP" WN9 model from the "model" partition */
    s_srmodels = esp_srmodel_init("model");
    if (!s_srmodels) {
        ESP_LOGE(TAG, "esp_srmodel_init(\"model\") failed — check partitions.csv");
        return ESP_FAIL;
    }
    s_wn_name = esp_srmodel_filter(s_srmodels, ESP_WN_PREFIX, "hiesp");
    if (!s_wn_name) {
        ESP_LOGE(TAG, "no hiesp WakeNet model — check CONFIG_SR_WN_WN9_HIESP");
        return ESP_ERR_NOT_FOUND;
    }
    s_wn_iface = esp_wn_handle_from_name(s_wn_name);
    if (!s_wn_iface) {
        ESP_LOGE(TAG, "no handler for model %s", s_wn_name);
        return ESP_ERR_NOT_FOUND;
    }
    s_wn_data = s_wn_iface->create(s_wn_name, DET_MODE_90);
    if (!s_wn_data) {
        ESP_LOGE(TAG, "wakenet create failed for %s", s_wn_name);
        return ESP_FAIL;
    }
    s_wn_chunk = s_wn_iface->get_samp_chunksize(s_wn_data);
    s_wn_buf = heap_caps_calloc(s_wn_chunk, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_wn_buf) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "WakeNet \"%s\" ready (wake word: \"%s\", chunk=%d, rate=%d, det_threshold=%.2f)",
             s_wn_name, s_wn_iface->get_word_name(s_wn_data, 1),
             s_wn_chunk, s_wn_iface->get_samp_rate(s_wn_data),
             s_wn_iface->get_det_threshold(s_wn_data, 1));

    /* Cache the "Yes?" ack at boot so it plays instantly on wake */
    if (!ack_fetch()) {
        ESP_LOGW(TAG, "ack not cached (backend down?) — falling back to beep");
    }

    /* Gap-free mic stream for the wake engine (prio-7 mic_feed task shovels
     * DMA into a PSRAM ring; without it the wake task loses DMA data). */
    esp_err_t serr = tool_audio_rx_stream_start();
    if (serr != ESP_OK) {
        ESP_LOGE(TAG, "mic feeder failed to start: %s", esp_err_to_name(serr));
        return serr;
    }

    s_running = true;

    /* The internal heap is too fragmented after WiFi/TLS/telegram come up to
     * hand out a contiguous task stack (field failure), so the stack lives in
     * PSRAM via xTaskCreateStatic
     * (needs CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y). */
    s_voice_stack = heap_caps_malloc(VOICE_TASK_STACK * sizeof(StackType_t),
                                     MALLOC_CAP_SPIRAM);
    s_voice_tcb = heap_caps_malloc(sizeof(StaticTask_t),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_voice_stack && s_voice_tcb) {
        s_voice_task = xTaskCreateStatic(voice_task, "voice_wake",
                                         VOICE_TASK_STACK, NULL,
                                         VOICE_TASK_PRIO, s_voice_stack,
                                         s_voice_tcb);
    }
    if (!s_voice_task) {
        /* Fallback: classic dynamic creation with a small internal stack */
        free(s_voice_stack); s_voice_stack = NULL;
        free(s_voice_tcb);   s_voice_tcb = NULL;
        BaseType_t ok = xTaskCreatePinnedToCore(voice_task, "voice_wake",
                                                6 * 1024, NULL,
                                                VOICE_TASK_PRIO, &s_voice_task,
                                                VOICE_TASK_CORE);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "failed to create voice task (static + dynamic both failed)");
            s_running = false;
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "voice task using small internal-stack fallback");
    }
    ESP_LOGI(TAG, "voice task created (%s stack)",
             s_voice_stack ? "12 KB PSRAM" : "6 KB internal");
    return ESP_OK;
}

void voice_client_stop(void)
{
    s_running = false;
    tool_audio_rx_stream_stop();   /* stop the mic feeder too */
}
