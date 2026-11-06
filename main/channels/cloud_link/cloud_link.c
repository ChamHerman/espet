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

#include "bus/message_bus.h"

static const char *TAG = "cloud";

#define CLOUD_FRAME_MAX 1024

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
}

/* -------------------------------------------------- inbound frame handling */

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
    } else if (strcmp(type_str, "hello_ack") == 0) {
        ESP_LOGI(TAG, "cloud gateway accepted this device");
    } else if (strcmp(type_str, "pong") == 0) {
        /* keepalive */
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
            s_rx_total = data->payload_len;   /* swallow remaining slices */
            return;
        }
        if (!s_rx_buf) {
            s_rx_buf = heap_caps_malloc(CLOUD_FRAME_MAX + 1, MALLOC_CAP_SPIRAM);
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
        s_rx_buf[s_rx_len] = '\0';
        handle_frame(s_rx_buf);
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
