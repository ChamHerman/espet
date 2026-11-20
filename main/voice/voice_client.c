/*
 * voice_client.c — hands-free voice pipeline (VAD + backend Whisper wake-word verification & TTS)
 *
 * Flow:
 *   LISTEN   VAD monitors the I2S mic stream for speech activity (RMS > threshold).
 *            Continuous listening consumes virtually 0% CPU (simple RMS calculation).
 *   CAPTURE  When speech is detected, arms capture with a 250ms pre-roll from the
 *            ring buffer (so initial syllables like "Mi-" or "Hey" are preserved).
 *            Captures until 1.2s silence or 8s cap.
 *   VERIFY   Audio is sent to backend /api/voice/transcribe (Whisper).
 *            Whisper transcribes and checks for configured wake words ("Mimi", "Hey Mimi", "米米", etc.).
 *            - If wake word NOT present: silently ignored (zero false actions).
 *            - If one-shot command ("Hey Mimi turn on LED"):
 *                Remaining command is queued directly to agent ReAct loop!
 *            - If wake word only ("Mimi"):
 *                Plays pre-cached "Yes?" ack, opens command window, then queues follow-up.
 *   SPEAK    Agent reply routed to voice_client_speak() -> /api/voice/tts -> MAX98357A speaker.
 */

#include "voice/voice_client.h"
#include "mimi_config.h"
#include "display/display_status.h"
#include "sync/backend_discovery.h"
#include "tools/tool_audio.h"
#include "bus/message_bus.h"
#include "channels/telegram/telegram_bot.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "mbedtls/base64.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

/* debug: feeder-side DMA counters + ambient floor (defined in tool_audio.c) */
extern volatile uint32_t s_feeder_reads;
extern volatile uint32_t s_feeder_zeros;
extern volatile uint32_t s_ring_written;
extern volatile int32_t  s_mic_floor;   /* ambient RMS, updated by the feeder */

static const char *TAG = "voice";

/* -----------------------------------------------------------------------
 * Tuning
 * --------------------------------------------------------------------- */
#define VOICE_TASK_STACK       (12 * 1024)  /* stack lives in PSRAM */
#define VOICE_TASK_PRIO        3            /* low priority to not starve IDLE0 */
#define VOICE_TASK_CORE        0            /* Core 0 */

#define VAD_CHUNK_MS           32           /* 32 ms frame */
#define VAD_CHUNK_SAMPLES      (TOOL_AUDIO_SAMPLE_RATE * VAD_CHUNK_MS / 1000) /* 512 samples */
#define VAD_SPEECH_RMS_START   420          /* minimum trigger floor (quiet room).
                                             * Real threshold = max(this, 1.33x ambient floor),
                                             * capped at VAD_RMS_THRESHOLD_MAX: the field room sat
                                             * at ~9500 RMS steady (fan/TV + 2.5x mic gain) — 20x
                                             * this value — and re-triggered every ~1.5s while
                                             * nobody spoke. */
#define VAD_RMS_THRESHOLD_MAX  14000        /* adaptive cap. The field room sits at ~9500 RMS
                                             * steady (speech peaks only 10~16k), so the cap must
                                             * stay below speech peaks — 14000 keeps the device
                                             * responsive while pushing the trigger above most of
                                             * the ambient. Residual noise captures are silently
                                             * rejected by the backend confidence gate. */
#define VAD_TRIGGER_CONSEC     3            /* 3 consecutive frames (~96ms) required to trigger */
#define VAD_PREROLL_SAMPLES    4000         /* 250 ms pre-roll to capture full wake word prefix */

#define ECHO_GUARD_MS          700          /* mic discarded after ANY playback: the amp + room
                                             * keep ringing for hundreds of ms; field showed VAD
                                             * re-triggering 330 ms after TTS end and Whisper
                                             * hallucinating the wake word from the tail. */
#define ACK_SETTLE_MS          450          /* wait after the "Yes?" ack before opening the command
                                             * window so the ack echo is not captured as a command */

#define ACK_CACHE_BYTES        (96 * 1024)  /* "Yes?" WAV cache in PSRAM */

#define UTTERANCE_MAX_MS       2500         /* cap on wake word utterance (2.5s) */
#define CMD_MAX_MS             8000         /* hard cap on command capture (8.0s) */
#define CMD_MAX_SAMPLES        (TOOL_AUDIO_SAMPLE_RATE * CMD_MAX_MS / 1000)
#define CMD_LEAD_MS            3500         /* initial silence / wait-for-speech timeout (3.5s) */
#define CMD_SILENCE_MS         1200         /* endpointing trailing silence cutoff (1.2s) */
#define CMD_MIN_SPEECH_MS      300          /* minimum speech duration to validate real utterance (300ms) */
#define CMD_SILENCE_RMS        280          /* trailing silence baseline threshold */
#define VOICE_HTTP_TIMEOUT_MS  30000
#define VOICE_HTTP_RX_MAX      (2 * 1024 * 1024)  /* PSRAM */
#define VOICE_TX_CHUNK         (4 * 1024)

#define BEEP_ACK_FALLBACK_HZ   880
#define BEEP_ERR_HZ            220
#define BEEP_TIMEOUT_HZ        330
#define BEEP_MS                120
#define BEEP_VOL               0.4f

/* -----------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------- */
static TaskHandle_t         s_voice_task     = NULL;
static StackType_t         *s_voice_stack    = NULL;  /* PSRAM-allocated */
static StaticTask_t        *s_voice_tcb      = NULL;  /* internal RAM */
static volatile bool        s_running        = false;
static volatile bool        s_listen_enabled = true;  /* autonomous VAD listener toggle (always on by default) */
static volatile bool        s_busy           = false;  /* pipeline in flight */
static int64_t              s_busy_start_ms  = 0;      /* watchdog timer for agent thinking */
static volatile bool        s_abort_requested = false; /* interruption flag */
static volatile int64_t     s_mic_guard_until_ms = 0;  /* post-playback echo guard deadline */
static uint8_t             *s_ack_wav        = NULL;   /* "Yes?" cache */
static size_t               s_ack_wav_len    = 0;

void voice_client_request_abort(void)
{
    s_abort_requested = true;
    ESP_LOGW(TAG, "Voice interruption requested — aborting current agent turn");
}

bool voice_client_is_abort_requested(void)
{
    return s_abort_requested;
}

void voice_client_clear_abort(void)
{
    s_abort_requested = false;
}

/* -----------------------------------------------------------------------
 * Signal Processing Helpers
 * --------------------------------------------------------------------- */

/* Zero-mean (AC) RMS calculation:
 * Measures true acoustic waveform energy without being misled by DC bias,
 * ground bounce, or power-rail noise from OLED/peripherals. */
static inline int32_t calc_ac_rms(const int16_t *samples, size_t count)
{
    if (count == 0) return 0;
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += samples[i];
    }
    int32_t mean = (int32_t)(sum / (int64_t)count);
    int64_t var_sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t diff = (int32_t)samples[i] - mean;
        var_sum += (int64_t)diff * diff;
    }
    return (int32_t)sqrt((double)var_sum / (double)count);
}

/* Adaptive trigger threshold: fixed 420 was calibrated for a quiet room, but
 * the field room floor sits at ~9000 RMS (fan/TV + 2.5x mic gain), making the
 * device fire every ~1.5s on non-speech. The floor is measured by the always-
 * running mic feeder (s_mic_floor, see tool_audio.c); require a margin above it. */
static inline int32_t vad_trigger_threshold(void)
{
    int32_t floor = s_mic_floor;
    int32_t thr = floor + floor / 3;                           /* 1.33x floor */
    if (thr < VAD_SPEECH_RMS_START) thr = VAD_SPEECH_RMS_START;
    if (floor < VAD_RMS_THRESHOLD_MAX && thr > VAD_RMS_THRESHOLD_MAX) {
        thr = VAD_RMS_THRESHOLD_MAX;
    }
    if (thr <= floor) thr = floor + 300;
    return thr;
}

/* Arm the post-playback echo guard: drop mic input for ECHO_GUARD_MS so the
 * amp tail + room reverb can't trigger VAD (and then Whisper-hallucinate the
 * wake word from the TTS tail). */
static void mic_guard_start(uint32_t ms)
{
    int64_t until = esp_timer_get_time() / 1000 + (int64_t)ms;
    if (until > s_mic_guard_until_ms) {
        s_mic_guard_until_ms = until;
    }
}

static bool mic_guard_active(void)
{
    return (esp_timer_get_time() / 1000) < s_mic_guard_until_ms;
}

/* -----------------------------------------------------------------------
 * HTTP helpers (PSRAM buffers)
 * --------------------------------------------------------------------- */

static void voice_backend_url(char *out, size_t out_sz, const char *path)
{
    char base[96];
    backend_discovery_base_url(base, sizeof(base));
    snprintf(out, out_sz, "%s%s", base, path);
}

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
    bool oom;
    char oled_line1[17];
    char oled_line2[17];
} rx_buf_t;

static esp_err_t rx_event_handler(esp_http_client_event_t *evt)
{
    rx_buf_t *rx = (rx_buf_t *)evt->user_data;
    if (rx == NULL) {
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value) {
        if (strcasecmp(evt->header_key, "X-OLED-Line1") == 0) {
            strncpy(rx->oled_line1, evt->header_value, sizeof(rx->oled_line1) - 1);
            rx->oled_line1[sizeof(rx->oled_line1) - 1] = '\0';
        } else if (strcasecmp(evt->header_key, "X-OLED-Line2") == 0) {
            strncpy(rx->oled_line2, evt->header_value, sizeof(rx->oled_line2) - 1);
            rx->oled_line2[sizeof(rx->oled_line2) - 1] = '\0';
        }
        return ESP_OK;
    }
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len == 0) {
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
    char url[128];
    voice_backend_url(url, sizeof(url), "/api/voice/tts");
    rx_buf_t rx = { .buf = s_ack_wav, .len = 0, .cap = ACK_CACHE_BYTES, .oom = false };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = VOICE_HTTP_TIMEOUT_MS,
        .event_handler = rx_event_handler,
        .user_data = &rx,
        .buffer_size = VOICE_TX_CHUNK,
        .buffer_size_tx = VOICE_TX_CHUNK,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        tool_audio_speaker_session_end();
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
 * STT: POST raw PCM → {"text": ..., "raw_text": ..., "wake_matched": ...}
 * --------------------------------------------------------------------- */

bool voice_client_transcribe_full(const int16_t *pcm, size_t samples, voice_stt_result_t *out_res)
{
    if (!out_res) return false;
    memset(out_res, 0, sizeof(*out_res));
    if (!pcm || samples == 0) return false;

    size_t body_len = samples * sizeof(int16_t);
    rx_buf_t *rx = heap_caps_calloc(1, sizeof(rx_buf_t), MALLOC_CAP_SPIRAM);
    if (!rx) return false;
    rx->cap = 8 * 1024;
    rx->buf = heap_caps_calloc(1, rx->cap, MALLOC_CAP_SPIRAM);
    if (!rx->buf) {
        free(rx);
        return false;
    }

    char url[128];
    voice_backend_url(url, sizeof(url), "/api/voice/transcribe");

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = VOICE_HTTP_TIMEOUT_MS,
        .event_handler = rx_event_handler,
        .user_data = rx,
        .buffer_size = VOICE_TX_CHUNK,
        .buffer_size_tx = VOICE_TX_CHUNK,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(rx->buf);
        free(rx);
        return false;
    }
    esp_http_client_set_header(client, "Authorization", "Bearer " MIMI_BACKEND_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    {
        char rate_hdr[12];
        snprintf(rate_hdr, sizeof(rate_hdr), "%d", TOOL_AUDIO_SAMPLE_RATE);
        esp_http_client_set_header(client, "X-Audio-Rate", rate_hdr);
    }
    esp_http_client_set_post_field(client, (const char *)pcm, (int)body_len);

    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    uint32_t dt = (uint32_t)(esp_timer_get_time() / 1000) - t0;

    bool ok = false;
    if (err == ESP_OK && status == 200 && !rx->oom && rx->len > 0) {
        rx->buf[rx->len < rx->cap - 1 ? rx->len : rx->cap - 1] = '\0';
        cJSON *j = cJSON_Parse((const char *)rx->buf);
        if (j) {
            cJSON *w_match = cJSON_GetObjectItem(j, "wake_matched");
            if (cJSON_IsBool(w_match)) {
                out_res->wake_matched = cJSON_IsTrue(w_match);
            }
            cJSON *w_word = cJSON_GetObjectItem(j, "wake_word");
            if (cJSON_IsString(w_word) && w_word->valuestring[0] != '\0') {
                out_res->wake_word = strdup(w_word->valuestring);
            }
            cJSON *t = cJSON_GetObjectItem(j, "text");
            if (cJSON_IsString(t) && t->valuestring[0] != '\0') {
                out_res->text = strdup(t->valuestring);
            }
            cJSON *raw = cJSON_GetObjectItem(j, "raw_text");
            if (cJSON_IsString(raw) && raw->valuestring[0] != '\0') {
                out_res->raw_text = strdup(raw->valuestring);
            }
            cJSON_Delete(j);
            ok = true;
            ESP_LOGI(TAG, "STT %u ms, %u -> %u bytes: wake=%d ('%s'), cmd='%.60s', raw='%.60s'",
                     (unsigned)dt, (unsigned)body_len, (unsigned)rx->len,
                     out_res->wake_matched,
                     out_res->wake_word ? out_res->wake_word : "",
                     out_res->text ? out_res->text : "",
                     out_res->raw_text ? out_res->raw_text : "");
        }
    } else {
        ESP_LOGE(TAG, "STT FAILED: err=%s status=%d oom=%d len=%u (%u ms)",
                 esp_err_to_name(err), status, rx->oom, (unsigned)rx->len, (unsigned)dt);
        backend_discovery_invalidate();
    }

    free(rx->buf);
    free(rx);
    return ok;
}

char *voice_client_transcribe(const int16_t *pcm, size_t samples)
{
    voice_stt_result_t res;
    if (!voice_client_transcribe_full(pcm, samples, &res)) {
        return NULL;
    }
    char *ret = NULL;
    if (res.text && res.text[0] != '\0') {
        ret = res.text;
        res.text = NULL;
    } else if (res.raw_text && res.raw_text[0] != '\0') {
        ret = res.raw_text;
        res.raw_text = NULL;
    }
    free(res.text);
    free(res.raw_text);
    free(res.wake_word);
    return ret;
}

char *voice_client_vision_query_full(const uint8_t *jpeg_data, size_t jpeg_len, const char *query,
                                     char *out_l1, size_t l1_sz, char *out_l2, size_t l2_sz)
{
    if (out_l1 && l1_sz > 0) out_l1[0] = '\0';
    if (out_l2 && l2_sz > 0) out_l2[0] = '\0';
    if (!jpeg_data || jpeg_len == 0) return NULL;

    rx_buf_t *rx = heap_caps_calloc(1, sizeof(rx_buf_t), MALLOC_CAP_SPIRAM);
    if (!rx) return NULL;
    rx->cap = 16 * 1024;
    rx->buf = heap_caps_calloc(1, rx->cap, MALLOC_CAP_SPIRAM);
    if (!rx->buf) {
        free(rx);
        return NULL;
    }

    char url[160];
    voice_backend_url(url, sizeof(url), "/api/camera/vision-query");

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 40000,
        .event_handler = rx_event_handler,
        .user_data = rx,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(rx->buf);
        free(rx);
        return NULL;
    }

    esp_http_client_set_header(client, "Authorization", "Bearer " MIMI_BACKEND_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "image/jpeg");

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char dev_id[32];
    snprintf(dev_id, sizeof(dev_id), "ESP-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    esp_http_client_set_header(client, "X-Device-Id", dev_id);

    /* If query provided, base64-encode it into X-Query-B64 header */
    if (query && query[0] != '\0') {
        size_t qlen = strlen(query);
        size_t b64_max = ((qlen + 2) / 3) * 4 + 4;
        char *b64_buf = (char *)malloc(b64_max);
        if (b64_buf) {
            size_t olen = 0;
            if (mbedtls_base64_encode((unsigned char *)b64_buf, b64_max, &olen,
                                      (const unsigned char *)query, qlen) == 0) {
                b64_buf[olen] = '\0';
                esp_http_client_set_header(client, "X-Query-B64", b64_buf);
            }
            free(b64_buf);
        }
    }

    esp_http_client_set_post_field(client, (const char *)jpeg_data, (int)jpeg_len);

    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    uint32_t dt = (uint32_t)(esp_timer_get_time() / 1000) - t0;

    char *reply = NULL;
    if (err == ESP_OK && status == 200 && !rx->oom && rx->len > 0) {
        rx->buf[rx->len < rx->cap - 1 ? rx->len : rx->cap - 1] = '\0';
        cJSON *j = cJSON_Parse((const char *)rx->buf);
        if (j) {
            cJSON *rep_item = cJSON_GetObjectItem(j, "reply");
            if (cJSON_IsString(rep_item) && rep_item->valuestring[0] != '\0') {
                reply = strdup(rep_item->valuestring);
            }
            if (out_l1 && l1_sz > 0) {
                cJSON *l1_item = cJSON_GetObjectItem(j, "oled_l1");
                if (cJSON_IsString(l1_item) && l1_item->valuestring[0] != '\0') {
                    strncpy(out_l1, l1_item->valuestring, l1_sz - 1);
                    out_l1[l1_sz - 1] = '\0';
                }
            }
            if (out_l2 && l2_sz > 0) {
                cJSON *l2_item = cJSON_GetObjectItem(j, "oled_l2");
                if (cJSON_IsString(l2_item) && l2_item->valuestring[0] != '\0') {
                    strncpy(out_l2, l2_item->valuestring, l2_sz - 1);
                    out_l2[l2_sz - 1] = '\0';
                }
            }
            cJSON_Delete(j);
        }
        ESP_LOGI(TAG, "Vision query OK in %u ms (%u bytes img): '%.60s' (OLED: '%s' / '%s')",
                 (unsigned)dt, (unsigned)jpeg_len, reply ? reply : "",
                 out_l1 ? out_l1 : "", out_l2 ? out_l2 : "");
    } else {
        ESP_LOGE(TAG, "Vision query FAILED: err=%s status=%d len=%u (%u ms)",
                 esp_err_to_name(err), status, (unsigned)rx->len, (unsigned)dt);
        backend_discovery_invalidate();
    }

    free(rx->buf);
    free(rx);
    return reply;
}

char *voice_client_vision_query(const uint8_t *jpeg_data, size_t jpeg_len, const char *query)
{
    return voice_client_vision_query_full(jpeg_data, jpeg_len, query, NULL, 0, NULL, 0);
}

/* -----------------------------------------------------------------------
 * TTS speak: text → WAV → speaker
 * --------------------------------------------------------------------- */

static void word_wrap_oled(const char *text, char *l1, size_t l1_sz, char *l2, size_t l2_sz)
{
    if (l1 && l1_sz > 0) l1[0] = '\0';
    if (l2 && l2_sz > 0) l2[0] = '\0';
    if (!text || !*text || !l1 || !l2) return;

    size_t tlen = strlen(text);
    if (tlen < l1_sz) {
        strncpy(l1, text, l1_sz - 1);
        l1[l1_sz - 1] = '\0';
        return;
    }

    /* Find last space before index 16 */
    int split1 = 16;
    while (split1 > 0 && text[split1] != ' ' && text[split1] != '\0') {
        split1--;
    }
    if (split1 == 0) {
        split1 = 16; /* No space found in first 16 chars, hard break */
    }

    int copy1 = split1 < (int)l1_sz ? split1 : (int)l1_sz - 1;
    strncpy(l1, text, copy1);
    l1[copy1] = '\0';

    /* Advance past spaces for line 2 */
    const char *p2 = text + split1;
    while (*p2 == ' ') p2++;

    if (*p2) {
        int split2 = 16;
        size_t rem = strlen(p2);
        if (rem < (size_t)split2) {
            split2 = (int)rem;
        } else {
            while (split2 > 0 && p2[split2] != ' ' && p2[split2] != '\0') {
                split2--;
            }
            if (split2 == 0) split2 = 16;
        }
        int copy2 = split2 < (int)l2_sz ? split2 : (int)l2_sz - 1;
        strncpy(l2, p2, copy2);
        l2[copy2] = '\0';
    }
}

static uint8_t *voice_tts_fetch(const char *text, size_t *out_len,
                                char *out_l1, size_t out_l1_sz,
                                char *out_l2, size_t out_l2_sz)
{
    if (out_l1 && out_l1_sz > 0) out_l1[0] = '\0';
    if (out_l2 && out_l2_sz > 0) out_l2[0] = '\0';

    char *body = NULL;
    cJSON *j = cJSON_CreateObject();
    if (!j) return NULL;
    cJSON_AddStringToObject(j, "text", text);
    body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body) return NULL;

    rx_buf_t *rx = heap_caps_calloc(1, sizeof(rx_buf_t), MALLOC_CAP_SPIRAM);
    if (!rx) {
        free(body);
        return NULL;
    }
    rx->cap = VOICE_HTTP_RX_MAX;
    rx->buf = heap_caps_calloc(1, rx->cap, MALLOC_CAP_SPIRAM);
    if (!rx->buf) {
        free(body);
        free(rx);
        return NULL;
    }

    char url[128];
    voice_backend_url(url, sizeof(url), "/api/voice/tts");

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = VOICE_HTTP_TIMEOUT_MS,
        .event_handler = rx_event_handler,
        .user_data = rx,
        .buffer_size = VOICE_TX_CHUNK,
        .buffer_size_tx = VOICE_TX_CHUNK,
        .crt_bundle_attach = esp_crt_bundle_attach,
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
        rx->buf = NULL; /* Transfer buffer ownership to caller */
        if (out_l1 && rx->oled_line1[0]) {
            strncpy(out_l1, rx->oled_line1, out_l1_sz - 1);
            out_l1[out_l1_sz - 1] = '\0';
        }
        if (out_l2 && rx->oled_line2[0]) {
            strncpy(out_l2, rx->oled_line2, out_l2_sz - 1);
            out_l2[out_l2_sz - 1] = '\0';
        }
        ESP_LOGI(TAG, "TTS %u ms, %u bytes WAV (OLED: '%s' / '%s')",
                 (unsigned)dt, (unsigned)rx->len,
                 out_l1 ? out_l1 : "", out_l2 ? out_l2 : "");
    } else {
        ESP_LOGE(TAG, "TTS FAILED: err=%s status=%d oom=%d len=%u (%u ms)",
                 esp_err_to_name(err), status, rx->oom, (unsigned)rx->len, (unsigned)dt);
        display_status_set("Voice err", "check backend");
        backend_discovery_invalidate();
    }
    if (rx->buf) {
        free(rx->buf);
    }
    free(rx);
    return wav;
}

static void voice_speak_internal(const char *text, const char *display_l1, const char *display_l2)
{
    if (voice_client_is_abort_requested()) {
        ESP_LOGI(TAG, "voice_speak_internal: skipping playback (turn aborted by user)");
        return;
    }
    tool_audio_speaker_session_begin();
    size_t wav_len = 0;
    char fetched_l1[17] = {0};
    char fetched_l2[17] = {0};
    uint8_t *wav = voice_tts_fetch(text, &wav_len, fetched_l1, sizeof(fetched_l1), fetched_l2, sizeof(fetched_l2));
    if (!wav) {
        tool_audio_speaker_session_end();
        tool_audio_beep(BEEP_ERR_HZ, BEEP_MS * 2, BEEP_VOL);
        return;
    }
    if (voice_client_is_abort_requested()) {
        ESP_LOGI(TAG, "voice_speak_internal: dropping fetched WAV (turn aborted by user)");
        free(wav);
        tool_audio_speaker_session_end();
        return;
    }
    if ((display_l1 && display_l1[0]) || (display_l2 && display_l2[0])) {
        display_status_set_text_timed(display_l1 ? display_l1 : "", display_l2 ? display_l2 : "", 8000);
    } else if (!display_status_is_held()) {
        if (fetched_l1[0] || fetched_l2[0]) {
            display_status_set_text_timed(fetched_l1, fetched_l2, 8000);
        } else if (text && text[0]) {
            char l1[17] = {0};
            char l2[17] = {0};
            word_wrap_oled(text, l1, sizeof(l1), l2, sizeof(l2));
            display_status_set_text_timed(l1, l2, 8000);
        }
    }
    esp_err_t perr = tool_audio_play_wav(wav, wav_len);
    free(wav);
    tool_audio_speaker_session_end();
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "WAV playback failed: %s", esp_err_to_name(perr));
    }
    /* Extend hold for 5 seconds after speech completes (Speech Duration + 5s buffer) */
    display_status_extend_hold(5000);
}

/* -----------------------------------------------------------------------
 * Command phase: capture → STT → agent bus (used for 2-step wake)
 * --------------------------------------------------------------------- */

static void voice_run_command(void)
{
    /* Wait out the "Yes?" ack echo before opening the command window: the amp
     * tail + room reverb outlive the ack by hundreds of ms (field: the window
     * captured the echo and Whisper hallucinated a wake word from it →
     * "no command given" loop). */
    vTaskDelay(pdMS_TO_TICKS(ACK_SETTLE_MS));

    /* Clear cache right before arming so no pre-existing audio is read */
    tool_audio_clear_cache();

    if (!tool_audio_capture_arm(CMD_MAX_SAMPLES)) {
        ESP_LOGW(TAG, "command capture could not arm (bus busy)");
        tool_audio_clear_cache();
        s_busy = false;
        s_busy_start_ms = 0;
        display_status_idle();
        return;
    }

    display_status_set("Listening...", "Say command");
    ESP_LOGI(TAG, "[CMD] Listening for command (countdown %d ms starts now after 'Yes?')...", CMD_MAX_MS);

    int16_t scratch[VAD_CHUNK_SAMPLES];
    size_t total = 0;
    int64_t start_ms = esp_timer_get_time() / 1000;
    int64_t last_speech_ms = 0;
    uint32_t speech_ms = 0;
    bool heard_speech = false;

    bool capturing = true;
    while (capturing && s_running) {
        size_t written = 0;
        bool full = false;
        capturing = tool_audio_rx_read(scratch, VAD_CHUNK_SAMPLES, &written,
                                       &total, &full, VAD_CHUNK_MS);

        if (written > 0) {
            int32_t rms = calc_ac_rms(scratch, written);
            /* Only count as real speech if rms reaches speech level */
            if (rms >= vad_trigger_threshold()) {
                if (!heard_speech) {
                    heard_speech = true;
                }
                last_speech_ms = esp_timer_get_time() / 1000;
                speech_ms += VAD_CHUNK_MS;
            }
        }
        if (full) {
            break;
        }

        int64_t now = esp_timer_get_time() / 1000;

        /* 1. Initial silence timeout (no speech within 3.5s) */
        if (!heard_speech && (now - start_ms >= CMD_LEAD_MS)) {
            ESP_LOGI(TAG, "[CMD] wait-for-speech timeout (%d ms) — no speech detected", CMD_LEAD_MS);
            telegram_send_to_last_chat("⏱️ Listening timed out (no speech heard).");
            tool_audio_beep(BEEP_TIMEOUT_HZ, BEEP_MS, BEEP_VOL);
            tool_audio_clear_cache();
            s_busy = false;
            s_busy_start_ms = 0;
            display_status_idle();
            return;
        }

        /* 2. Trailing silence cutoff (speech ended with >= 1.2s silence) */
        if (heard_speech && (speech_ms >= CMD_MIN_SPEECH_MS) && (now - last_speech_ms >= CMD_SILENCE_MS)) {
            ESP_LOGI(TAG, "[CMD] speech ended with %lld ms trailing silence (speech_ms=%u)",
                     (long long)(now - last_speech_ms), (unsigned)speech_ms);
            break;
        }

        /* 3. Hard cap reached (8.0s) */
        if (now - start_ms >= CMD_MAX_MS) {
            ESP_LOGI(TAG, "[CMD] full %d ms countdown reached", CMD_MAX_MS);
            break;
        }
    }

    tool_audio_capture_disarm();
    uint32_t dur = (uint32_t)(esp_timer_get_time() / 1000) - (uint32_t)start_ms;
    ESP_LOGI(TAG, "[CMD] captured %u samples (%u ms)", (unsigned)total, (unsigned)dur);

    if (total < TOOL_AUDIO_SAMPLE_RATE / 2) {
        ESP_LOGW(TAG, "command capture too short (< 500ms)");
        telegram_send_to_last_chat("⚠️ Command too short or unclear.");
        tool_audio_beep(BEEP_TIMEOUT_HZ, BEEP_MS, BEEP_VOL);
        tool_audio_clear_cache();
        s_busy = false;
        s_busy_start_ms = 0;
        display_status_idle();
        return;
    }

    size_t samples = 0;
    const int16_t *pcm = tool_audio_get_last_capture(&samples, &dur);
    if (!pcm) {
        tool_audio_clear_cache();
        s_busy = false;
        s_busy_start_ms = 0;
        display_status_idle();
        return;
    }

    ESP_LOGI(TAG, "=== voice pipeline: STT (%u samples / %u ms) ===",
             (unsigned)samples, (unsigned)dur);

    voice_stt_result_t stt_res = {0};
    bool ok = voice_client_transcribe_full(pcm, samples, &stt_res);
    if (!ok) {
        ESP_LOGW(TAG, "transcript failed or backend unavailable");
        telegram_send_to_last_chat("⚠️ Speech recognition failed or backend unavailable.");
        tool_audio_beep(BEEP_TIMEOUT_HZ, BEEP_MS, BEEP_VOL);
        tool_audio_clear_cache();
        s_busy = false;
        s_busy_start_ms = 0;
        display_status_idle();
        return;
    }

    /* Extract actual command:
     * - If wake_matched and text is non-empty: user gave a command.
     * - If wake_matched but text is empty: user ONLY repeated wake word ("米米"), no command!
     * - If !wake_matched and raw_text is non-empty: user gave a direct command ("开灯").
     */
    char *cmd_text = NULL;
    if (stt_res.wake_matched) {
        if (stt_res.text && strlen(stt_res.text) > 0) {
            cmd_text = strdup(stt_res.text);
        } else {
            ESP_LOGW(TAG, "[CMD] only wake word ('%s') heard during command phase — no command given",
                     stt_res.wake_word ? stt_res.wake_word : "mimi");
        }
    } else {
        if (stt_res.raw_text && strlen(stt_res.raw_text) > 0) {
            cmd_text = strdup(stt_res.raw_text);
        }
    }

    free(stt_res.text);
    free(stt_res.raw_text);
    free(stt_res.wake_word);

    if (!cmd_text || strlen(cmd_text) < 1) {
        ESP_LOGW(TAG, "no valid command text — clearing cache and returning to listen");
        if (cmd_text) free(cmd_text);
        telegram_send_to_last_chat("⚠️ Didn't catch that. Please speak again.");
        tool_audio_beep(BEEP_TIMEOUT_HZ, BEEP_MS, BEEP_VOL);
        tool_audio_clear_cache();
        s_busy = false;
        s_busy_start_ms = 0;
        display_status_idle();
        return;
    }

    /* Provide immediate feedback of recognized user command to Telegram */
    char you_msg[384];
    snprintf(you_msg, sizeof(you_msg), "🗣️ You: \"%s\"", cmd_text);
    telegram_send_to_last_chat(you_msg);

    mimi_msg_t msg = {0};
    strlcpy(msg.channel, MIMI_CHAN_SYSTEM, sizeof(msg.channel));
    strlcpy(msg.chat_id, "voice", sizeof(msg.chat_id));
    msg.content = cmd_text;                    /* bus takes ownership */
    esp_err_t perr = message_bus_push_inbound(&msg);
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "inbound push failed: %s", esp_err_to_name(perr));
        free(cmd_text);
        tool_audio_clear_cache();
        s_busy = false;
        s_busy_start_ms = 0;
        display_status_idle();
    } else {
        ESP_LOGI(TAG, "transcript queued to agent: %.60s", cmd_text);
        s_busy = true;
        s_busy_start_ms = esp_timer_get_time() / 1000;
        display_status_set("Thinking...", "");
        ESP_LOGI(TAG, "mic paused while agent is thinking/replying");
    }
}

/* -----------------------------------------------------------------------
 * VAD Wake listener task
 * --------------------------------------------------------------------- */

static void voice_task(void *arg)
{
    int16_t vad_chunk[VAD_CHUNK_SAMPLES];
    size_t  vad_filled = 0;
    int     consec_speech = 0;

    /* ---- listen diagnostics (rate-limited) ---- */
    uint32_t dbg_frames   = 0;
    uint64_t dbg_rms_sum  = 0;
    int32_t  dbg_rms_peak = 0;
    uint32_t dbg_loud     = 0;
    uint32_t dbg_empties  = 0;
    int64_t  dbg_last_report_ms = esp_timer_get_time() / 1000;
    bool     was_paused = false;

    ESP_LOGI(TAG, "software wake listener running — say \"Hey ESPET\" or \"ESPET\" (sounds like \"Expert\") "
                  "(adaptive VAD: feeder floor, base thr=%d, x1.33)",
             VAD_SPEECH_RMS_START);

    while (s_running) {
        /* When autonomous VAD listening is disabled, sleep quietly without sampling or logging */
        if (!s_listen_enabled) {
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }

        /* ---- 5-second debug report & watchdog ---- */
        int64_t now = esp_timer_get_time() / 1000;
        if (now - dbg_last_report_ms >= 5000) {
            uint32_t avg = dbg_frames ? (uint32_t)(dbg_rms_sum / dbg_frames) : 0;
            ESP_LOGI(TAG, "[listen] 5s: frames=%u avg_rms=%u peak_rms=%d loud=%u paused/empty=%u",
                     (unsigned)dbg_frames, avg, (int)dbg_rms_peak,
                     (unsigned)dbg_loud, (unsigned)dbg_empties);
            ESP_LOGI(TAG, "[debug] feeder: dma_reads=%u zero_reads=%u ring_written=%u | busy=%d spk_busy=%d",
                     (unsigned)s_feeder_reads, (unsigned)s_feeder_zeros,
                     (unsigned)s_ring_written,
                     s_busy, tool_audio_is_busy());
            dbg_frames = 0; dbg_rms_sum = 0; dbg_rms_peak = 0; dbg_loud = 0; dbg_empties = 0;
            dbg_last_report_ms = now;

            /* Auto-recover if agent turn dropped without clearing s_busy */
            if (s_busy && s_busy_start_ms > 0 && (now - s_busy_start_ms >= 35000)) {
                ESP_LOGW(TAG, "agent thinking watchdog (35s) — clearing busy flag");
                s_busy = false;
                s_busy_start_ms = 0;
                display_status_idle();
            }
        }

        /* Pause while speaker plays, an audio recording session is active, or the
         * post-playback echo guard is still active (amp tail + room reverb).
         * NOTE: We do NOT pause merely because the agent is thinking/running tools (s_busy),
         * allowing hands-free voice commands to be heard and to interrupt long operations! */
        if (tool_audio_is_speaker_busy() || mic_guard_active() || tool_audio_is_busy()) {
            dbg_empties++;
            was_paused = true;
            consec_speech = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (was_paused) {
            was_paused = false;
            tool_audio_clear_cache();   /* drop everything buffered during the pause */
            vad_filled = 0;
            consec_speech = 0;
        }

        /* Read one VAD chunk from the ring */
        while (vad_filled < VAD_CHUNK_SAMPLES) {
            size_t got = tool_audio_ring_read(vad_chunk + vad_filled,
                                              VAD_CHUNK_SAMPLES - vad_filled,
                                              20);
            if (got == 0) {
                dbg_empties++;
                break;
            }
            vad_filled += got;
        }
        if (vad_filled < VAD_CHUNK_SAMPLES) {
            vTaskDelay(1);
            continue;
        }
        vad_filled = 0;

        /* Calculate AC-RMS of the chunk (zero-mean) */
        int32_t rms = calc_ac_rms(vad_chunk, VAD_CHUNK_SAMPLES);
        int32_t vad_thr = vad_trigger_threshold();
        dbg_frames++;
        dbg_rms_sum += rms;
        if (rms > dbg_rms_peak) dbg_rms_peak = rms;

        /* If ambient floor or signal is persistently railed (> 25000 RMS), do not trigger VAD.
         * Report wiring error periodically and pause to avoid permanent capture loops. */
        if (s_mic_floor > 25000 && rms > 25000) {
            consec_speech = 0;
            static int64_t s_last_railed_log = 0;
            if (now - s_last_railed_log >= 5000) {
                s_last_railed_log = now;
                ESP_LOGW(TAG, "[VAD] mic signal railed (rms=%d, floor=%d) — check INMP441 GND/VDD/SD wiring",
                         (int)rms, (int)s_mic_floor);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (rms >= vad_thr) {
            consec_speech++;
            dbg_loud++;
            ESP_LOGI(TAG, "🎤 [VAD] speech energy: rms=%d (frame %d/%d, thr=%d)",
                     rms, consec_speech, VAD_TRIGGER_CONSEC, vad_thr);
        } else {
            consec_speech = 0;
        }

        /* Noise floor is maintained by the always-running mic feeder
         * (s_mic_floor in tool_audio.c) — it keeps updating even while this
         * task is blocked in capture/STT, which is exactly when the old
         * in-loop estimator fell behind and never converged. */

        /* Need N consecutive frames (~96 ms) above the adaptive threshold */
        if (consec_speech >= VAD_TRIGGER_CONSEC) {
            consec_speech = 0;
            s_busy = true;
            ESP_LOGI(TAG, ">>> SPEECH DETECTED (rms=%d) — capturing utterance with 250ms pre-roll...", rms);

            /* Arm capture with 250ms (4000 samples) pre-roll from the ring */
            if (!tool_audio_capture_arm_preroll(CMD_MAX_SAMPLES, VAD_PREROLL_SAMPLES)) {
                ESP_LOGW(TAG, "could not arm capture (bus busy)");
                s_busy = false;
                continue;
            }

            int32_t floor = s_mic_floor;
            int32_t silence_cutoff = floor + floor / 4;  /* 1.25x floor */
            if (silence_cutoff < CMD_SILENCE_RMS) silence_cutoff = CMD_SILENCE_RMS;
            if (silence_cutoff > VAD_RMS_THRESHOLD_MAX) silence_cutoff = VAD_RMS_THRESHOLD_MAX;

            int16_t scratch[VAD_CHUNK_SAMPLES];
            size_t total = 0;
            int64_t last_voice_ms = esp_timer_get_time() / 1000;
            int64_t start_ms = last_voice_ms;

            bool capturing = true;
            while (capturing && s_running) {
                size_t written = 0;
                bool full = false;
                capturing = tool_audio_rx_read(scratch, VAD_CHUNK_SAMPLES, &written,
                                               &total, &full, VAD_CHUNK_MS);

                if (written > 0) {
                    int32_t chunk_rms = calc_ac_rms(scratch, written);
                    if (chunk_rms >= silence_cutoff) {
                        last_voice_ms = esp_timer_get_time() / 1000;
                    }
                }
                if (full) {
                    break;
                }
                int64_t cur_ms = esp_timer_get_time() / 1000;
                /* Stop on trailing silence */
                if (cur_ms - last_voice_ms >= CMD_SILENCE_MS) {
                    ESP_LOGI(TAG, "[utterance] silence timeout (%d ms)", CMD_SILENCE_MS);
                    break;
                }
                /* Hard cap at 1.5s for wake word utterance */
                if (cur_ms - start_ms >= UTTERANCE_MAX_MS) {
                    ESP_LOGI(TAG, "[utterance] max %d ms reached", UTTERANCE_MAX_MS);
                    break;
                }
            }

            tool_audio_capture_disarm();
            uint32_t dur = (uint32_t)(esp_timer_get_time() / 1000) - (uint32_t)start_ms;
            ESP_LOGI(TAG, "[utterance] captured %u samples (%u ms)", (unsigned)total, (unsigned)dur);

            /* If captured audio is too short (< 0.25s), discard */
            if (total < TOOL_AUDIO_SAMPLE_RATE / 4) {
                ESP_LOGI(TAG, "[utterance] too short (< 250ms), ignoring");
                s_busy = false;
                continue;
            }

            size_t cap_samples = 0;
            uint32_t cap_dur = 0;
            const int16_t *pcm = tool_audio_get_last_capture(&cap_samples, &cap_dur);
            if (!pcm || cap_samples == 0) {
                s_busy = false;
                continue;
            }

            voice_stt_result_t stt_res;
            bool ok = voice_client_transcribe_full(pcm, cap_samples, &stt_res);
            if (!ok) {
                ESP_LOGW(TAG, "[utterance] STT failed or backend unavailable");
                s_busy = false;
                continue;
            }

            if (!stt_res.wake_matched) {
                ESP_LOGI(TAG, "[wake] not matched — raw='%.60s' (ignoring ambient talk)",
                         stt_res.raw_text ? stt_res.raw_text : "");
                free(stt_res.text);
                free(stt_res.raw_text);
                free(stt_res.wake_word);
                tool_audio_ring_flush();
                consec_speech = 0;
                vad_filled = 0;
                s_busy = false;
                continue;
            }

            /* --- WAKE WORD MATCHED! --- */
            ESP_LOGI(TAG, "*** WAKE WORD MATCHED: '%s' (raw='%.60s') ***",
                     stt_res.wake_word ? stt_res.wake_word : "",
                     stt_res.raw_text ? stt_res.raw_text : "");

            if (s_busy) {
                ESP_LOGW(TAG, "*** WAKE WORD INTERRUPT: In-flight agent turn detected — requesting abort ***");
                voice_client_request_abort();
            }

            /* Feedback will be sent below depending on one-shot vs wake-only */
            display_status_set("Listening...", "");

            if (stt_res.text && strlen(stt_res.text) > 0) {
                /* Case 1: One-shot command! E.g. "Hey Mimi turn on the LED" */
                ESP_LOGI(TAG, "*** ONE-SHOT COMMAND: '%.60s' ***", stt_res.text);
                display_status_set("Thinking...", "");
                char notif[384];
                snprintf(notif, sizeof(notif), "🎤 Wake word matched: %s\n🗣️ You: \"%s\"",
                         stt_res.wake_word ? stt_res.wake_word : "ESPET", stt_res.text);
                telegram_send_to_last_chat(notif);


                mimi_msg_t msg = {0};
                strlcpy(msg.channel, MIMI_CHAN_SYSTEM, sizeof(msg.channel));
                strlcpy(msg.chat_id, "voice", sizeof(msg.chat_id));
                msg.content = strdup(stt_res.text);
                esp_err_t perr = message_bus_push_inbound(&msg);
                if (perr != ESP_OK) {
                    ESP_LOGE(TAG, "inbound push failed: %s", esp_err_to_name(perr));
                    free(msg.content);
                    tool_audio_clear_cache();
                    s_busy = false;
                    s_busy_start_ms = 0;
                    display_status_idle();
                } else {
                    ESP_LOGI(TAG, "one-shot command queued to agent: %.60s", stt_res.text);
                    s_busy = true;
                    s_busy_start_ms = esp_timer_get_time() / 1000;
                    ESP_LOGI(TAG, "mic paused while agent is thinking/replying");
                }
            } else {
                /* Case 2: Wake word only! E.g. "Mimi" or "Hey Mimi" */
                ESP_LOGI(TAG, "*** WAKE WORD ONLY — playing 'Yes?' and listening for command ***");
                telegram_send_to_last_chat("🎤 I heard you! Listening for your command...");
                ack_play();
                /* voice_run_command() waits out ACK_SETTLE_MS internally, which
                 * covers the ack's amp tail + room reverb. Only the ring/DMA
                 * needs flushing here. */
                tool_audio_clear_cache();

                voice_run_command();
            }

            free(stt_res.text);
            free(stt_res.raw_text);
            free(stt_res.wake_word);
            /* Note: s_busy remains true until voice_client_speak finishes */
        }

        vTaskDelay(1);
    }

    ESP_LOGI(TAG, "voice task stopped");
    s_voice_task = NULL;
    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

bool voice_client_speak(const char *text)
{
    return voice_client_speak_with_display(text, NULL, NULL);
}

bool voice_client_speak_with_display(const char *text, const char *line1, const char *line2)
{
    if (voice_client_is_abort_requested()) {
        ESP_LOGI(TAG, "Skipping voice speak — aborted by user interruption");
        tool_audio_clear_cache();
        s_busy = false;
        s_busy_start_ms = 0;
        return false;
    }
    if (!text || !*text) {
        tool_audio_clear_cache();
        s_busy = false;
        s_busy_start_ms = 0;
        display_status_idle();
        return false;
    }
    s_busy = true;
    voice_speak_internal(text, line1, line2);
    /* Echo guard: drop mic input for ECHO_GUARD_MS after playback. The amp
     * tail + room reverb outlive the WAV by hundreds of ms; without this the
     * VAD re-triggered 330 ms after TTS end and Whisper hallucinated the wake
     * word from the tail — an endless self-wake loop (field log 2026-10-09). */
    mic_guard_start(ECHO_GUARD_MS);
    /* Clear cache so wake listener starts completely fresh */
    tool_audio_clear_cache();
    s_busy = false;
    s_busy_start_ms = 0;
    display_status_idle();
    ESP_LOGI(TAG, "voice reply complete — %d ms echo guard, listening for wake up again", ECHO_GUARD_MS);
    return true;
}

esp_err_t voice_client_start(void)
{
    if (s_voice_task) {
        return ESP_OK;
    }

    /* Cache the "Yes?" ack at boot so it plays instantly on wake */
    if (!ack_fetch()) {
        ESP_LOGW(TAG, "ack not cached (backend down?) — falling back to beep");
    }

    /* Gap-free mic stream for VAD listening */
    esp_err_t serr = tool_audio_rx_stream_start();
    if (serr != ESP_OK) {
        ESP_LOGE(TAG, "mic feeder failed to start: %s", esp_err_to_name(serr));
        return serr;
    }

    s_running = true;

    /* Stack allocated in PSRAM via xTaskCreateStatic if available */
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
    tool_audio_rx_stream_stop();
}

void voice_client_set_listen(bool enable)
{
    s_listen_enabled = enable;
    ESP_LOGI(TAG, "VAD real-time wake listener %s", enable ? "ENABLED" : "DISABLED");
    if (!enable) {
        tool_audio_clear_cache();
    }
}

bool voice_client_is_listen_enabled(void)
{
    return s_listen_enabled;
}
