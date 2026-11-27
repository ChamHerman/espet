/*
 * cloud_link.c — hybrid-mode cloud channel (Plan C-Hybrid)
 *
 * One persistent WebSocket (device = client) to the backend gateway.
 * The device keeps its full agent; only the Telegram transport moves to the
 * backend, which removes the per-6 s polling connections that caused lwIP
 * socket starvation on the constrained ESP32 TCP stack.
 */

#include "channels/cloud_link/cloud_link.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <time.h>
#include <sys/time.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mimi_config.h"
#include "sync/backend_discovery.h"
#include "sync/config_sync.h"
#include "tools/tool_audio.h"
#include "tools/tool_camera.h"
#include "voice/voice_client.h"

#include "channels/telegram/telegram_bot.h"
#include "bus/message_bus.h"

static const char *TAG = "cloud";

#define CLOUD_FRAME_MAX (36 * 1024)

static esp_websocket_client_handle_t s_client;
static volatile bool s_running;
static char s_device_id[20];   /* "ESP-XXXXXXXXXXXX" + NUL */
static char s_active_url[160]; /* URI the current s_client was built with */
static TaskHandle_t s_supervisor;

/* LLM relay state — one request in flight (agent loop is single-threaded).
 * The WS event task copies the llm.response body and gives the semaphore. */
#define CLOUD_LLM_RESP_MAX (32 * 1024)
static SemaphoreHandle_t s_llm_sema;
static char *s_llm_resp;             /* PSRAM */
static size_t s_llm_resp_len;
static int s_llm_status;
static bool s_llm_ok;
static uint32_t s_llm_next_id = 1;
static volatile uint32_t s_llm_active_id;

/* Frame reassembly: WEBSOCKET_EVENT_DATA may deliver a text frame in slices
 * (data->payload_offset / data->payload_len describe the whole frame). */
static char *s_rx_buf;
static size_t s_rx_len;
static size_t s_rx_total;

/* ------------------------------------------------------------------ send */

static void send_json(const char *json)
{
    if (!s_client || !esp_websocket_client_is_connected(s_client)) {
        return;
    }
    int written = esp_websocket_client_send_text(
        s_client, json, (int)strlen(json), pdMS_TO_TICKS(3000));
    if (written < 0) {
        ESP_LOGW(TAG, "send failed (not connected?)");
    }
}

static void send_hello(void)
{
    char hello[96];
    snprintf(hello, sizeof(hello),
             "{\"type\":\"hello\",\"device_id\":\"%s\",\"fw\":\"%s\"}",
             s_device_id, esp_get_idf_version());
    send_json(hello);
    ESP_LOGI(TAG, "hello sent as %s", s_device_id);
    cloud_link_send_sync_report();
}
esp_err_t cloud_link_send_sync_report(void)
{
    if (!s_client || !esp_websocket_client_is_connected(s_client)) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "device_sync_report");
    cJSON_AddStringToObject(root, "device_id", s_device_id);

    /* 1. Read MEMORY.md */
    FILE *f_mem = fopen(MIMI_MEMORY_FILE, "r");
    if (f_mem) {
        fseek(f_mem, 0, SEEK_END);
        long sz = ftell(f_mem);
        fseek(f_mem, 0, SEEK_SET);
        if (sz > 0 && sz <= 16384) {
            char *mem_buf = malloc(sz + 1);
            if (mem_buf) {
                size_t nr = fread(mem_buf, 1, sz, f_mem);
                mem_buf[nr] = '\0';
                cJSON_AddStringToObject(root, "memory_md", mem_buf);
                free(mem_buf);
            }
        }
        fclose(f_mem);
    } else {
        cJSON_AddStringToObject(root, "memory_md", "");
    }

    /* 2. Read cron.json */
    FILE *f_cron = fopen(MIMI_CRON_FILE, "r");
    cJSON *jobs_arr = NULL;
    if (f_cron) {
        fseek(f_cron, 0, SEEK_END);
        long csz = ftell(f_cron);
        fseek(f_cron, 0, SEEK_SET);
        if (csz > 0 && csz <= 16384) {
            char *cron_buf = malloc(csz + 1);
            if (cron_buf) {
                size_t cnr = fread(cron_buf, 1, csz, f_cron);
                cron_buf[cnr] = '\0';
                cJSON *cron_root = cJSON_Parse(cron_buf);
                if (cron_root) {
                    cJSON *jitem = cJSON_GetObjectItem(cron_root, "jobs");
                    if (jitem && cJSON_IsArray(jitem)) {
                        jobs_arr = cJSON_Duplicate(jitem, true);
                    }
                    cJSON_Delete(cron_root);
                }
                free(cron_buf);
            }
        }
        fclose(f_cron);
    }
    if (!jobs_arr) {
        jobs_arr = cJSON_CreateArray();
    }
    cJSON_AddItemToObject(root, "jobs", jobs_arr);

    char *rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!rendered) {
        return ESP_ERR_NO_MEM;
    }
    send_json(rendered);
    free(rendered);
    ESP_LOGI(TAG, "device_sync_report sent");
    return ESP_OK;
}


typedef struct {
    size_t samples;
    uint32_t dur;
    double rms;
} transcribe_req_t;

#define CLOUD_WORKER_STACK_WORDS (8192 / sizeof(StackType_t))
static StackType_t *s_trans_stack = NULL;
static StaticTask_t s_trans_tcb;
static volatile bool s_trans_active = false;

static void transcribe_task_fn(void *arg)
{
    transcribe_req_t *req = (transcribe_req_t *)arg;
    const int16_t *pcm = tool_audio_get_last_capture(&req->samples, &req->dur);
    voice_stt_result_t stt_res = {0};
    bool ok = (pcm && req->samples > 0) ? voice_client_transcribe_full(pcm, req->samples, &stt_res) : false;
    tool_audio_manual_capture_release();

    const char *text_out = "";
    if (ok) {
        if (stt_res.raw_text && stt_res.raw_text[0] != '\0') {
            text_out = stt_res.raw_text;
        } else if (stt_res.text && stt_res.text[0] != '\0') {
            text_out = stt_res.text;
        }
    }

    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "type", "record_result");
        cJSON_AddBoolToObject(root, "ok", true);
        cJSON_AddNumberToObject(root, "samples", (double)req->samples);
        cJSON_AddNumberToObject(root, "duration_ms", (double)req->dur);
        cJSON_AddNumberToObject(root, "rms", req->rms);
        cJSON_AddStringToObject(root, "transcript", text_out);
        if (!ok) {
            cJSON_AddStringToObject(root, "error", "stt_failed");
        }
        char *rendered = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (rendered) {
            send_json(rendered);
            free(rendered);
        }
    }

    free(stt_res.text);
    free(stt_res.raw_text);
    free(stt_res.wake_word);
    free(req);
    s_trans_active = false;
    vTaskDelete(NULL);
}

typedef struct {
    char *text;
    char *line1;
    char *line2;
} tts_say_req_t;

static StackType_t *s_tts_stack = NULL;
static StaticTask_t s_tts_tcb;
static volatile bool s_tts_active = false;

static void tts_say_task_fn(void *arg)
{
    tts_say_req_t *req = (tts_say_req_t *)arg;
    if (req) {
        voice_client_speak_with_display(req->text, req->line1, req->line2);
        free(req->text);
        free(req->line1);
        free(req->line2);
        free(req);
    }
    s_tts_active = false;
    vTaskDelete(NULL);
}

/* -------------------------------------------------- inbound frame handling */
static void sync_bound_chat(const cJSON *root)
{
    const cJSON *chat = cJSON_GetObjectItem(root, "chat_id");
    if (cJSON_IsString(chat) && chat->valuestring[0] != '\0') {
        telegram_set_last_chat_id(chat->valuestring);
    }
}


static void handle_frame(char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "non-JSON frame: %.60s", json_str);
        return;
    }
    const cJSON *type = cJSON_GetObjectItem(root, "type");
    const char *type_str = cJSON_IsString(type) ? type->valuestring : "";

    if (strcmp(type_str, "message_in") == 0) {
        const cJSON *chat = cJSON_GetObjectItem(root, "chat_id");
        const cJSON *text = cJSON_GetObjectItem(root, "text");
        sync_bound_chat(root);
        if (cJSON_IsString(chat) && cJSON_IsString(text)) {
            mimi_msg_t msg = {0};
            strlcpy(msg.channel, MIMI_CHAN_CLOUD, sizeof(msg.channel));
            strlcpy(msg.chat_id, chat->valuestring, sizeof(msg.chat_id));
            msg.content = strdup(text->valuestring);
            if (!msg.content) {
                ESP_LOGE(TAG, "OOM for inbound message");
                cJSON_Delete(root);
                return;
            }
            if (message_bus_push_inbound(&msg) != ESP_OK) {
                ESP_LOGW(TAG, "inbound queue full — dropping '%.40s'",
                         msg.content);
                free(msg.content);
            } else {
                ESP_LOGI(TAG, "message_in chat %s: %.60s",
                         chat->valuestring, text->valuestring);
            }
        }
    } else if (strcmp(type_str, "llm.response") == 0) {
        const cJSON *req_id = cJSON_GetObjectItem(root, "request_id");
        if (!cJSON_IsNumber(req_id) ||
            (uint32_t)req_id->valuedouble != s_llm_active_id) {
            ESP_LOGW(TAG, "stale llm.response (id %d) — ignored",
                     cJSON_IsNumber(req_id) ? (int)req_id->valuedouble : -1);
        } else {
            const cJSON *body = cJSON_GetObjectItem(root, "body");
            const cJSON *ok = cJSON_GetObjectItem(root, "ok");
            const cJSON *status = cJSON_GetObjectItem(root, "status");
            s_llm_ok = cJSON_IsBool(ok) ? cJSON_IsTrue(ok) : false;
            s_llm_status = cJSON_IsNumber(status) ? (int)status->valuedouble : 0;
            const char *body_str = cJSON_IsString(body) ? body->valuestring : "";
            s_llm_resp_len = 0;
            if (s_llm_resp) {
                size_t n = strlen(body_str);
                if (n > CLOUD_LLM_RESP_MAX) n = CLOUD_LLM_RESP_MAX;
                memcpy(s_llm_resp, body_str, n);
                s_llm_resp[n] = '\0';
                s_llm_resp_len = n;
            }
            if (s_llm_sema) {
                xSemaphoreGive(s_llm_sema);
            }
        }
    } else if (strcmp(type_str, "record_start") == 0) {
        esp_err_t err = tool_audio_manual_record_start();
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"type\":\"record_status\",\"status\":\"%s\",\"ok\":%s}",
                 (err == ESP_OK) ? "recording" : "error",
                 (err == ESP_OK) ? "true" : "false");
        send_json(resp);
    } else if (strcmp(type_str, "record_stop") == 0) {
        size_t samples = 0;
        uint32_t dur = 0;
        double rms = 0.0;
        esp_err_t err = tool_audio_manual_record_stop(&samples, &dur, &rms);
        if (err != ESP_OK || samples == 0) {
            tool_audio_manual_capture_release();
            send_json("{\"type\":\"record_result\",\"ok\":false,\"error\":\"no_audio\"}");
        } else if (s_trans_active) {
            tool_audio_manual_capture_release();
            send_json("{\"type\":\"record_result\",\"ok\":false,\"error\":\"stt_busy\"}");
        } else {
            if (!s_trans_stack) {
                s_trans_stack = heap_caps_malloc(CLOUD_WORKER_STACK_WORDS * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
            }
            transcribe_req_t *req = malloc(sizeof(transcribe_req_t));
            if (req && s_trans_stack) {
                req->samples = samples;
                req->dur = dur;
                req->rms = rms;
                s_trans_active = true;
                TaskHandle_t h = xTaskCreateStatic(transcribe_task_fn, "stt_trans",
                                                   CLOUD_WORKER_STACK_WORDS, req, 5,
                                                   s_trans_stack, &s_trans_tcb);
                if (!h) {
                    s_trans_active = false;
                    free(req);
                    tool_audio_manual_capture_release();
                    send_json("{\"type\":\"record_result\",\"ok\":false,\"error\":\"task_fail\"}");
                }
            } else {
                free(req);
                tool_audio_manual_capture_release();
                send_json("{\"type\":\"record_result\",\"ok\":false,\"error\":\"oom\"}");
            }
        }
    } else if (strcmp(type_str, "record_abort") == 0) {
        tool_audio_manual_record_abort();
        send_json("{\"type\":\"record_status\",\"status\":\"aborted\",\"ok\":true}");
    } else if (strcmp(type_str, "voice_listen") == 0) {
        const cJSON *en = cJSON_GetObjectItem(root, "enabled");
        bool enable = cJSON_IsBool(en) ? cJSON_IsTrue(en) : false;
        voice_client_set_listen(enable);
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"type\":\"voice_listen_status\",\"enabled\":%s,\"ok\":true}",
                 enable ? "true" : "false");
        send_json(resp);
    } else if (strcmp(type_str, "tts_say") == 0) {
        const cJSON *text = cJSON_GetObjectItem(root, "text");
        const cJSON *line1 = cJSON_GetObjectItem(root, "display_title");
        const cJSON *line2 = cJSON_GetObjectItem(root, "display_body");
        const char *s = cJSON_IsString(text) ? text->valuestring : "";
        if (s[0] != '\0') {
            if (s_tts_active) {
                send_json("{\"type\":\"tts_say_status\",\"ok\":false,\"error\":\"tts_busy\"}");
            } else {
                if (!s_tts_stack) {
                    s_tts_stack = heap_caps_malloc(CLOUD_WORKER_STACK_WORDS * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
                }
                tts_say_req_t *req = calloc(1, sizeof(tts_say_req_t));
                if (req && s_tts_stack) {
                    req->text = strdup(s);
                    req->line1 = cJSON_IsString(line1) ? strdup(line1->valuestring) : NULL;
                    req->line2 = cJSON_IsString(line2) ? strdup(line2->valuestring) : NULL;
                    s_tts_active = true;
                    TaskHandle_t h = req->text
                        ? xTaskCreateStatic(tts_say_task_fn, "tts_say",
                                            CLOUD_WORKER_STACK_WORDS, req, 4,
                                            s_tts_stack, &s_tts_tcb)
                        : NULL;
                    if (!h) {
                        s_tts_active = false;
                        free(req->text);
                        free(req->line1);
                        free(req->line2);
                        free(req);
                        send_json("{\"type\":\"tts_say_status\",\"ok\":false,\"error\":\"task_fail\"}");
                    } else {
                        send_json("{\"type\":\"tts_say_status\",\"ok\":true}");
                    }
                } else {
                    free(req);
                    send_json("{\"type\":\"tts_say_status\",\"ok\":false,\"error\":\"oom\"}");
                }
            }
        }
    } else if (strcmp(type_str, "camera_stream_start") == 0) {
        const cJSON *fps_item = cJSON_GetObjectItem(root, "fps");
        int fps = cJSON_IsNumber(fps_item) ? (int)fps_item->valuedouble : MIMI_CAM_STREAM_FPS_DEFAULT;
        if (fps < 1) fps = 1;
        if (fps > MIMI_CAM_STREAM_FPS_MAX) fps = MIMI_CAM_STREAM_FPS_MAX;
        esp_err_t err = tool_camera_stream_start(fps);
        char resp[160];
        snprintf(resp, sizeof(resp),
                 "{\"type\":\"camera_stream_status\",\"streaming\":%s,\"fps\":%d,\"ok\":%s}",
                 tool_camera_stream_is_running() ? "true" : "false", fps,
                 (err == ESP_OK) ? "true" : "false");
        send_json(resp);
    } else if (strcmp(type_str, "camera_stream_stop") == 0) {
        esp_err_t err = tool_camera_stream_stop();
        char resp[144];
        snprintf(resp, sizeof(resp),
                 "{\"type\":\"camera_stream_status\",\"streaming\":false,\"ok\":%s}",
                 (err == ESP_OK) ? "true" : "false");
        send_json(resp);
    } else if (strcmp(type_str, "hello_ack") == 0) {
        ESP_LOGI(TAG, "cloud gateway accepted this device");
        sync_bound_chat(root);
        const cJSON *ep = cJSON_GetObjectItem(root, "server_epoch");
        if (ep && cJSON_IsNumber(ep) && ep->valuedouble > MIMI_MIN_VALID_EPOCH) {
            struct timeval tv = { .tv_sec = (time_t)ep->valuedouble, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "Clock synced from hello_ack: epoch %lld", (long long)tv.tv_sec);
        }
    } else if (strcmp(type_str, "time_sync") == 0) {
        const cJSON *ep = cJSON_GetObjectItem(root, "epoch");
        if (ep && cJSON_IsNumber(ep) && ep->valuedouble > MIMI_MIN_VALID_EPOCH) {
            struct timeval tv = { .tv_sec = (time_t)ep->valuedouble, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "Clock synced from time_sync frame: epoch %lld", (long long)tv.tv_sec);
        }
    } else if (strcmp(type_str, "bind_sync") == 0) {
        sync_bound_chat(root);
        ESP_LOGI(TAG, "received bind_sync from gateway");
        /* keepalive */
    } else if (strcmp(type_str, "sync_now") == 0) {
        config_sync_trigger();
        ESP_LOGI(TAG, "Received sync_now from backend — triggered immediate config sync");
    } else if (strcmp(type_str, "error") == 0) {
        const cJSON *detail = cJSON_GetObjectItem(root, "detail");
        ESP_LOGW(TAG, "gateway error: %s",
                 cJSON_IsString(detail) ? detail->valuestring : "?");
    }
    cJSON_Delete(root);
}

static void handle_text_chunk(esp_websocket_event_data_t *data)
{
    if (data->data_len <= 0) {
        return;
    }
    /* Start of a new frame: reset the accumulator. */
    if (data->payload_offset == 0) {
        if (data->payload_len > CLOUD_FRAME_MAX) {
            ESP_LOGW(TAG, "frame too large (%d) — dropped", data->payload_len);
            s_rx_total = (data->data_len < data->payload_len) ? (size_t)data->payload_len : 0;
            return;
        }
        if (!s_rx_buf) {
            s_rx_buf = heap_caps_calloc(1, CLOUD_FRAME_MAX + 1, MALLOC_CAP_SPIRAM);
            if (!s_rx_buf) {
                return;
            }
        }
        s_rx_len = 0;
        s_rx_total = data->payload_len;
    }

    if (s_rx_total > CLOUD_FRAME_MAX) {
        /* oversized frame mid-swallow */
    } else {
        size_t space = CLOUD_FRAME_MAX - s_rx_len;
        size_t copy = (size_t)data->data_len > space ? space
                                                     : (size_t)data->data_len;
        memcpy(s_rx_buf + s_rx_len, data->data_ptr, copy);
        s_rx_len += copy;
    }

    if ((size_t)(data->payload_offset + data->data_len) >= s_rx_total) {
        if (s_rx_total <= CLOUD_FRAME_MAX && s_rx_buf) {
            s_rx_buf[s_rx_len] = '\0';
            handle_frame(s_rx_buf);
        }
        s_rx_len = 0;
        s_rx_total = 0;
    }
}

/* ------------------------------------------------------------- event loop */

static void cloud_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to cloud gateway");
        send_hello();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected — auto-reconnect active");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x1 || data->op_code == 0x0) { /* text or cont */
            handle_text_chunk(data);
        }
        if (data->op_code == 0x9) {
            /* server PING — component answers PONG automatically */
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "WS error (reconnect will retry)");
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ API */

/* Build the WS URL from the discovered (or compiled-in) backend address.
 * The URL embeds the device API key, so never log it verbatim. */
static void build_ws_url(char *url, size_t url_sz)
{
    char base[96];
    backend_discovery_base_url(base, sizeof(base));   /* http://ip:8000 */
    if (strncmp(base, "http://", 7) == 0) {
        snprintf(url, url_sz, "ws://%s/api/cloud/ws/device?key=%s",
                 base + 7, MIMI_BACKEND_API_KEY);
    } else if (strncmp(base, "https://", 8) == 0) {
        snprintf(url, url_sz, "wss://%s/api/cloud/ws/device?key=%s",
                 base + 8, MIMI_BACKEND_API_KEY);
    } else {
        snprintf(url, url_sz, "ws://%s/api/cloud/ws/device?key=%s",
                 base, MIMI_BACKEND_API_KEY);
    }
}

/* (Re)create the WebSocket with the CURRENT discovery result. */
static esp_err_t cloud_open(void)
{
    char url[160];
    build_ws_url(url, sizeof(url));

    esp_websocket_client_config_t cfg = {
        .uri = url,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,
        .buffer_size = 4096,
        .task_stack = 8192,
        .disable_auto_reconnect = false,
        .enable_close_reconnect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_websocket_client_init failed");
        return ESP_FAIL;
    }
    esp_err_t err = esp_websocket_register_events(
        s_client, WEBSOCKET_EVENT_ANY, cloud_event_handler, NULL);
    if (err != ESP_OK) {
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return err;
    }
    err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_websocket_client_start failed: %s",
                 esp_err_to_name(err));
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return err;
    }
    strlcpy(s_active_url, url, sizeof(s_active_url));
    /* Log without the URL — it embeds the device API key. */
    ESP_LOGI(TAG, "cloud link started (%s://%s:*/ws/device, device %s)",
             strncmp(url, "wss", 3) == 0 ? "wss" : "ws",
             strstr(url, "://") + 3, s_device_id);
    return ESP_OK;
}

/* The WS client reconnects to a FIXED URI forever, so a backend that moves IP
 * (or simply wasn't up when the compiled-in fallback was used) would keep this
 * link dead. Watch the discovery result and rebuild the client whenever the
 * address changes — this also covers boot ordering, where cloud_link_start()
 * runs before mDNS has resolved and initially dials the stale compiled IP. */
static void cloud_supervisor_task(void *arg)
{
    while (s_running) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        if (!s_running) {
            break;
        }
        char want[160];
        build_ws_url(want, sizeof(want));
        if (strcmp(want, s_active_url) != 0) {
            ESP_LOGW(TAG, "backend address changed — reconnecting cloud link");
            if (s_client) {
                esp_websocket_client_stop(s_client);
                esp_websocket_client_destroy(s_client);
                s_client = NULL;
            }
            cloud_open();
        }
    }
    s_supervisor = NULL;
    vTaskDelete(NULL);
}

esp_err_t cloud_link_start(void)
{
    if (s_running) {
        return ESP_OK;
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "ESP-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);


    s_running = true;
    esp_err_t err = cloud_open();
    if (err != ESP_OK) {
        s_running = false;
        return err;
    }

    if (!s_supervisor) {
        xTaskCreate(cloud_supervisor_task, "cloud_sup", 4096, NULL,
                    tskIDLE_PRIORITY + 2, &s_supervisor);
    }
    return ESP_OK;
}

void cloud_link_stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;
    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    s_active_url[0] = '\0';
    if (s_rx_buf) {
        heap_caps_free(s_rx_buf);
        s_rx_buf = NULL;
    }
    s_rx_len = s_rx_total = 0;
}

esp_err_t cloud_link_send_text(const char *chat_id, const char *text)
{
    if (!s_client || !esp_websocket_client_is_connected(s_client)) {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "message_out");
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "text", text);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    int written = esp_websocket_client_send_text(
        s_client, json, (int)strlen(json), pdMS_TO_TICKS(5000));
    free(json);
    return (written >= 0) ? ESP_OK : ESP_FAIL;
}


esp_err_t cloud_link_llm_request(const char *body, size_t body_len,
                                 char *resp_buf, size_t resp_cap,
                                 size_t *resp_len, int *status,
                                 uint32_t timeout_ms)
{
    if (resp_len) *resp_len = 0;
    if (status) *status = 0;
    if (!s_client || !esp_websocket_client_is_connected(s_client)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_llm_sema) {
        s_llm_sema = xSemaphoreCreateBinary();
    }
    if (!s_llm_sema || !resp_buf || resp_cap == 0) {
        return ESP_FAIL;
    }
    if (!s_llm_resp) {
        s_llm_resp = heap_caps_malloc(CLOUD_LLM_RESP_MAX + 1, MALLOC_CAP_SPIRAM);
    }
    if (!s_llm_resp) {
        return ESP_ERR_NO_MEM;
    }

    uint32_t req_id = s_llm_next_id++;
    s_llm_active_id = req_id;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "llm.request");
    cJSON_AddNumberToObject(root, "request_id", req_id);
    /* body is the NUL-terminated request JSON built by llm_proxy */
    (void)body_len;
    cJSON_AddItemToObject(root, "body", cJSON_CreateString(body));
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    int written = esp_websocket_client_send_text(s_client, json,
                                                (int)strlen(json),
                                                pdMS_TO_TICKS(5000));
    free(json);
    if (written < 0) {
        ESP_LOGE(TAG, "llm.request send failed");
        s_llm_active_id = 0;
        return ESP_FAIL;
    }

    if (xSemaphoreTake(s_llm_sema, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "llm.response timeout after %u ms", (unsigned)timeout_ms);
        s_llm_active_id = 0;
        return ESP_ERR_TIMEOUT;
    }

    size_t n = s_llm_resp_len;
    if (n > resp_cap - 1) n = resp_cap - 1;
    memcpy(resp_buf, s_llm_resp, n);
    resp_buf[n] = '\0';
    if (resp_len) *resp_len = n;
    if (status) *status = s_llm_status;
    s_llm_active_id = 0;
    return ESP_OK;
}

bool cloud_link_is_connected(void)
{
    return s_client && esp_websocket_client_is_connected(s_client);
}
