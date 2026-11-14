#include "telegram_bot.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "proxy/http_proxy.h"
#include "wifi/wifi_manager.h"
#if MIMI_CLOUD_LINK_ENABLE
#include "channels/cloud_link/cloud_link.h"
#include "voice/voice_client.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "telegram";

static char s_bot_token[128] = MIMI_SECRET_TG_TOKEN;
static char s_last_chat_id[64] = {0};
static int64_t s_update_offset = 0;
static int64_t s_last_saved_offset = -1;
static int64_t s_last_offset_save_us = 0;

#define TG_OFFSET_NVS_KEY            "update_offset"
#define TG_LAST_CHAT_NVS_KEY         "last_chat"
#define TG_DEDUP_CACHE_SIZE          64
#define TG_OFFSET_SAVE_INTERVAL_US   (5LL * 1000 * 1000)
#define TG_OFFSET_SAVE_STEP          10

static uint64_t s_seen_msg_keys[TG_DEDUP_CACHE_SIZE] = {0};
static size_t s_seen_msg_idx = 0;

/* HTTP response accumulator */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} http_resp_t;

static uint64_t fnv1a64(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    if (!s) {
        return h;
    }
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t make_msg_key(const char *chat_id, int msg_id)
{
    uint64_t h = fnv1a64(chat_id);
    return (h << 16) ^ (uint64_t)(msg_id & 0xFFFF) ^ ((uint64_t)msg_id << 32);
}

static bool seen_msg_contains(uint64_t key)
{
    for (size_t i = 0; i < TG_DEDUP_CACHE_SIZE; i++) {
        if (s_seen_msg_keys[i] == key) {
            return true;
        }
    }
    return false;
}

static void seen_msg_insert(uint64_t key)
{
    s_seen_msg_keys[s_seen_msg_idx] = key;
    s_seen_msg_idx = (s_seen_msg_idx + 1) % TG_DEDUP_CACHE_SIZE;
}

static void save_update_offset_if_needed(bool force)
{
    if (s_update_offset <= 0) {
        return;
    }

    int64_t now = esp_timer_get_time();
    bool should_save = force;
    if (!should_save && s_last_saved_offset >= 0) {
        if ((s_update_offset - s_last_saved_offset) >= TG_OFFSET_SAVE_STEP) {
            should_save = true;
        } else if ((now - s_last_offset_save_us) >= TG_OFFSET_SAVE_INTERVAL_US) {
            should_save = true;
        }
    } else if (!should_save) {
        should_save = true;
    }

    if (!should_save) {
        return;
    }

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_TG, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }

    if (nvs_set_i64(nvs, TG_OFFSET_NVS_KEY, s_update_offset) == ESP_OK) {
        if (nvs_commit(nvs) == ESP_OK) {
            s_last_saved_offset = s_update_offset;
            s_last_offset_save_us = now;
        }
    }
    nvs_close(nvs);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_t *resp = (http_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (resp->len + evt->data_len >= resp->cap) {
            size_t new_cap = resp->cap * 2;
            while (new_cap <= resp->len + evt->data_len) {
                new_cap *= 2;
            }
            char *tmp = heap_caps_realloc(resp->buf, new_cap, MALLOC_CAP_SPIRAM);
            if (!tmp) {
                ESP_LOGE(TAG, "Failed to grow response buffer to %u in PSRAM", (unsigned)new_cap);
                return ESP_ERR_NO_MEM;
            }
            resp->buf = tmp;
            resp->cap = new_cap;
        }
        memcpy(resp->buf + resp->len, evt->data, evt->data_len);
        resp->len += evt->data_len;
        resp->buf[resp->len] = '\0';
    }
    return ESP_OK;
}

/* ── Proxy path: manual HTTP over CONNECT tunnel ────────────── */

static char *tg_api_call_via_proxy(const char *path, const char *post_data)
{
    proxy_conn_t *conn = proxy_conn_open("api.telegram.org", 443,
                                          (MIMI_TG_POLL_TIMEOUT_S + 5) * 1000);
    if (!conn) return NULL;

    /* Build HTTP request */
    char header[512];
    int hlen;
    if (post_data) {
        hlen = snprintf(header, sizeof(header),
            "POST /bot%s/%s HTTP/1.1\r\n"
            "Host: api.telegram.org\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n",
            s_bot_token, path, (int)strlen(post_data));
    } else {
        hlen = snprintf(header, sizeof(header),
            "GET /bot%s/%s HTTP/1.1\r\n"
            "Host: api.telegram.org\r\n"
            "Connection: close\r\n\r\n",
            s_bot_token, path);
    }

    if (proxy_conn_write(conn, header, hlen) < 0) {
        proxy_conn_close(conn);
        return NULL;
    }
    if (post_data && proxy_conn_write(conn, post_data, strlen(post_data)) < 0) {
        proxy_conn_close(conn);
        return NULL;
    }

    /* Read response — accumulate until connection close */
    size_t cap = 8192, len = 0;
    char *buf = heap_caps_calloc(1, cap, MALLOC_CAP_SPIRAM);
    if (!buf) { proxy_conn_close(conn); return NULL; }

    int timeout = (MIMI_TG_POLL_TIMEOUT_S + 5) * 1000;
    while (1) {
        if (len + 1024 >= cap) {
            cap *= 2;
            char *tmp = heap_caps_realloc(buf, cap, MALLOC_CAP_SPIRAM);
            if (!tmp) break;
            buf = tmp;
        }
        int n = proxy_conn_read(conn, buf + len, cap - len - 1, timeout);
        if (n <= 0) break;
        len += n;
    }
    buf[len] = '\0';
    proxy_conn_close(conn);

    /* Skip HTTP headers — find \r\n\r\n */
    char *body = strstr(buf, "\r\n\r\n");
    if (!body) { free(buf); return NULL; }
    body += 4;

    /* Return just the body */
    char *result = strdup(body);
    free(buf);
    return result;
}

/* ── Direct path: esp_http_client ───────────────────────────── */

static char *tg_api_call_direct(const char *method, const char *post_data)
{
    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/%s", s_bot_token, method);
    ESP_LOGI(TAG, "tg_api_call: %s", url);

    http_resp_t resp = {
        .buf = heap_caps_calloc(1, 8192, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = 8192,
    };
    if (!resp.buf) return NULL;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = (MIMI_TG_POLL_TIMEOUT_S + 5) * 1000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed for %s", method);
        free(resp.buf);
        return NULL;
    }

    if (post_data) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP perform failed for %s: %s (status %d)", method, esp_err_to_name(err), status);
        free(resp.buf);
        return NULL;
    }

    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "HTTP %s returned status %d: %.120s", method, status, resp.buf ? resp.buf : "");
    }

    return resp.buf;
}

static char *tg_api_call(const char *method, const char *post_data)
{
    if (http_proxy_is_enabled()) {
        ESP_LOGD(TAG, "tg_api_call %s via proxy", method);
        return tg_api_call_via_proxy(method, post_data);
    }
    return tg_api_call_direct(method, post_data);
}

static bool tg_response_is_ok(const char *resp, const char **out_desc)
{
    if (out_desc) {
        *out_desc = NULL;
    }
    if (!resp) {
        return false;
    }

    cJSON *root = cJSON_Parse(resp);
    if (root) {
        cJSON *ok_field = cJSON_GetObjectItem(root, "ok");
        bool ok = cJSON_IsTrue(ok_field);
        if (!ok && out_desc) {
            cJSON *desc = cJSON_GetObjectItem(root, "description");
            if (desc && cJSON_IsString(desc)) {
                *out_desc = desc->valuestring;
            }
        }
        cJSON_Delete(root);
        return ok;
    }

    /* Proxy or gateway can occasionally return non-standard payload framing. */
    if (strstr(resp, "\"ok\":true") != NULL) {
        return true;
    }

    return false;
}

static void process_updates(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "cJSON_Parse failed for updates (len=%u)", (unsigned)strlen(json_str));
        return;
    }

    cJSON *ok = cJSON_GetObjectItem(root, "ok");
    if (!cJSON_IsTrue(ok)) {
        cJSON *desc = cJSON_GetObjectItem(root, "description");
        ESP_LOGW(TAG, "getUpdates returned ok=false: %s", (desc && cJSON_IsString(desc)) ? desc->valuestring : "unknown");
        cJSON_Delete(root);
        return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(root);
        return;
    }

    int update_count = cJSON_GetArraySize(result);
    if (update_count > 0) {
        ESP_LOGI(TAG, "Received %d Telegram update(s)", update_count);
    }

    cJSON *update;
    cJSON_ArrayForEach(update, result) {
        char *upd_str = cJSON_PrintUnformatted(update);
        ESP_LOGI(TAG, "RAW UPDATE: %s", upd_str ? upd_str : "(null)");
        free(upd_str);

        /* Track offset and advance */
        cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
        int64_t uid = -1;
        if (cJSON_IsNumber(update_id)) {
            uid = (int64_t)update_id->valuedouble;
        }
        if (uid >= 0) {
            s_update_offset = uid + 1;
            save_update_offset_if_needed(false);
        }

        /* Extract message */
        cJSON *message = cJSON_GetObjectItem(update, "message");
        if (!message) {
            ESP_LOGW(TAG, "Update has no 'message' field");
            continue;
        }

        cJSON *text = cJSON_GetObjectItem(message, "text");
        if (!text || !cJSON_IsString(text)) {
            ESP_LOGW(TAG, "Message has no 'text' string");
            continue;
        }

        cJSON *chat = cJSON_GetObjectItem(message, "chat");
        if (!chat) {
            ESP_LOGW(TAG, "Message has no 'chat' object");
            continue;
        }

        cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
        if (!chat_id) {
            ESP_LOGW(TAG, "Chat has no 'id'");
            continue;
        }

        int msg_id_val = -1;
        cJSON *message_id = cJSON_GetObjectItem(message, "message_id");
        if (cJSON_IsNumber(message_id)) {
            msg_id_val = (int)message_id->valuedouble;
        }

        char chat_id_str[32];
        if (cJSON_IsString(chat_id) && chat_id->valuestring) {
            strncpy(chat_id_str, chat_id->valuestring, sizeof(chat_id_str) - 1);
            chat_id_str[sizeof(chat_id_str) - 1] = '\0';
        } else if (cJSON_IsNumber(chat_id)) {
            snprintf(chat_id_str, sizeof(chat_id_str), "%.0f", chat_id->valuedouble);
        } else {
            ESP_LOGW(TAG, "Chat id is neither string nor number");
            continue;
        }

        /* Store last active chat ID for outbound debug/event messages.
         * Persisted (only on change) so it survives reboots. */
        if (strcmp(s_last_chat_id, chat_id_str) != 0) {
            strncpy(s_last_chat_id, chat_id_str, sizeof(s_last_chat_id) - 1);
            nvs_handle_t nvs;
            if (nvs_open(MIMI_NVS_TG, NVS_READWRITE, &nvs) == ESP_OK) {
                if (nvs_set_str(nvs, TG_LAST_CHAT_NVS_KEY, chat_id_str) == ESP_OK) {
                    nvs_commit(nvs);
                }
                nvs_close(nvs);
            }
        }

        if (msg_id_val >= 0) {
            uint64_t msg_key = make_msg_key(chat_id_str, msg_id_val);
            if (seen_msg_contains(msg_key)) {
                ESP_LOGW(TAG, "Drop duplicate message update_id=%" PRId64 " chat=%s message_id=%d",
                         uid, chat_id_str, msg_id_val);
                continue;
            }
            seen_msg_insert(msg_key);
        }

        ESP_LOGI(TAG, "Message update_id=%" PRId64 " message_id=%d from chat %s: %.40s...",
                 uid, msg_id_val, chat_id_str, text->valuestring);

        /* Push to inbound bus */
        mimi_msg_t msg = {0};
        strncpy(msg.channel, MIMI_CHAN_TELEGRAM, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, chat_id_str, sizeof(msg.chat_id) - 1);
        msg.content = strdup(text->valuestring);
        if (msg.content) {
            if (message_bus_push_inbound(&msg) != ESP_OK) {
                ESP_LOGW(TAG, "Inbound queue full, drop telegram message");
                free(msg.content);
            }
        }
    }

    cJSON_Delete(root);
}

static void telegram_poll_task(void *arg)
{
    ESP_LOGI(TAG, "Telegram polling task started (proxy=%s, offset=%" PRId64 ")",
             http_proxy_is_enabled() ? "YES" : "NO", s_update_offset);

    while (1) {
        if (!wifi_manager_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (s_bot_token[0] == '\0') {
            ESP_LOGW(TAG, "No bot token configured, waiting...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        char params[128];
        snprintf(params, sizeof(params),
                 "getUpdates?offset=%" PRId64 "&timeout=%d&limit=5",
                 s_update_offset, MIMI_TG_POLL_TIMEOUT_S);

        char *resp = tg_api_call(params, NULL);
        if (resp) {
            process_updates(resp);
            free(resp);
        } else {
            /* Back off on error */
            ESP_LOGW(TAG, "Poll failed, backoff 3s");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
}

/* --- Public API --- */

esp_err_t telegram_bot_init(void)
{
    /* Build-time secret takes priority */
    if (MIMI_SECRET_TG_TOKEN[0] != '\0') {
        strncpy(s_bot_token, MIMI_SECRET_TG_TOKEN, sizeof(s_bot_token) - 1);
    }

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_TG, NVS_READWRITE, &nvs) == ESP_OK) {
        char nvs_token[128] = {0};
        size_t len = sizeof(nvs_token);
        bool has_nvs_token = (nvs_get_str(nvs, MIMI_NVS_KEY_TG_TOKEN, nvs_token, &len) == ESP_OK && nvs_token[0]);

        if (s_bot_token[0] == '\0' && has_nvs_token) {
            strncpy(s_bot_token, nvs_token, sizeof(s_bot_token) - 1);
        }

        int64_t offset = 0;
        if (nvs_get_i64(nvs, TG_OFFSET_NVS_KEY, &offset) == ESP_OK && offset > 0) {
            s_update_offset = offset;
            s_last_saved_offset = offset;
            ESP_LOGI(TAG, "Loaded Telegram update offset: %" PRId64, s_update_offset);
        }

        /* Restore the last active chat from a previous session so button
         * photo/video sends work before the first inbound message arrives. */
        size_t chat_len = sizeof(s_last_chat_id);
        if (nvs_get_str(nvs, TG_LAST_CHAT_NVS_KEY, s_last_chat_id, &chat_len) == ESP_OK &&
            s_last_chat_id[0]) {
            ESP_LOGI(TAG, "Restored last active chat: %s", s_last_chat_id);
        }
        nvs_close(nvs);
    }

    if (s_bot_token[0]) {
        ESP_LOGI(TAG, "Telegram bot token loaded (len=%d)", (int)strlen(s_bot_token));
    } else {
        ESP_LOGW(TAG, "No Telegram bot token. Use CLI: set_tg_token <TOKEN>");
    }
    return ESP_OK;
}

esp_err_t telegram_bot_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        telegram_poll_task, "tg_poll",
        MIMI_TG_POLL_STACK, NULL,
        MIMI_TG_POLL_PRIO, NULL, MIMI_TG_POLL_CORE);

    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t telegram_send_message(const char *chat_id, const char *text)
{
    if (!chat_id || chat_id[0] == '\0') {
        ESP_LOGW(TAG, "Skipping Telegram send: chat_id is empty");
        return ESP_OK;
    }

    if (s_bot_token[0] == '\0') {
        ESP_LOGW(TAG, "Cannot send: no bot token");
        return ESP_ERR_INVALID_STATE;
    }

    /* Split long messages at 4096-char boundary */
    size_t text_len = strlen(text);
    size_t offset = 0;
    int all_ok = 1;

    while (offset < text_len) {
        size_t chunk = text_len - offset;
        if (chunk > MIMI_TG_MAX_MSG_LEN) {
            chunk = MIMI_TG_MAX_MSG_LEN;
        }

        /* Build JSON body */
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "chat_id", chat_id);

        /* Create null-terminated chunk */
        char *segment = malloc(chunk + 1);
        if (!segment) {
            cJSON_Delete(body);
            return ESP_ERR_NO_MEM;
        }
        memcpy(segment, text + offset, chunk);
        segment[chunk] = '\0';

        cJSON_AddStringToObject(body, "text", segment);
        cJSON_AddStringToObject(body, "parse_mode", "Markdown");

        char *json_str = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
        free(segment);

        if (!json_str) {
            all_ok = 0;
            offset += chunk;
            continue;
        }

        ESP_LOGI(TAG, "Sending telegram chunk to %s (%d bytes)", chat_id, (int)chunk);
        char *resp = tg_api_call("sendMessage", json_str);
        free(json_str);

        int sent_ok = 0;
        bool markdown_failed = false;
        if (resp) {
            const char *desc = NULL;
            sent_ok = tg_response_is_ok(resp, &desc);
            if (!sent_ok) {
                markdown_failed = true;
                ESP_LOGI(TAG, "Markdown rejected by Telegram for %s: %s",
                         chat_id, desc ? desc : "unknown");
            }
        }

        if (!sent_ok) {
            /* Retry without parse_mode */
            cJSON *body2 = cJSON_CreateObject();
            cJSON_AddStringToObject(body2, "chat_id", chat_id);
            char *seg2 = malloc(chunk + 1);
            if (seg2) {
                memcpy(seg2, text + offset, chunk);
                seg2[chunk] = '\0';
                cJSON_AddStringToObject(body2, "text", seg2);
                free(seg2);
            }
            char *json2 = cJSON_PrintUnformatted(body2);
            cJSON_Delete(body2);
            if (json2) {
                char *resp2 = tg_api_call("sendMessage", json2);
                free(json2);
                if (resp2) {
                    const char *desc2 = NULL;
                    sent_ok = tg_response_is_ok(resp2, &desc2);
                    if (!sent_ok) {
                        ESP_LOGE(TAG, "Plain send failed: %s", desc2 ? desc2 : "unknown");
                        ESP_LOGE(TAG, "Telegram raw response: %.300s", resp2);
                    }
                    free(resp2);
                } else {
                    ESP_LOGE(TAG, "Plain send failed: no HTTP response");
                }
            } else {
                ESP_LOGE(TAG, "Plain send failed: no JSON body");
            }
        }

        if (!sent_ok) {
            all_ok = 0;
        } else {
            if (markdown_failed) {
                ESP_LOGI(TAG, "Plain-text fallback succeeded for %s", chat_id);
            }
            ESP_LOGI(TAG, "Telegram send success to %s (%d bytes)", chat_id, (int)chunk);
        }

        free(resp);
        offset += chunk;
    }

    return all_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t telegram_set_token(const char *token)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_TG, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_TG_TOKEN, token));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_bot_token, token, sizeof(s_bot_token) - 1);
    ESP_LOGI(TAG, "Telegram bot token saved");
    return ESP_OK;
}

void telegram_set_last_chat_id(const char *chat_id)
{
    if (!chat_id || chat_id[0] == '\0') {
        return;
    }
    if (strcmp(s_last_chat_id, chat_id) != 0) {
        strncpy(s_last_chat_id, chat_id, sizeof(s_last_chat_id) - 1);
        s_last_chat_id[sizeof(s_last_chat_id) - 1] = '\0';
        nvs_handle_t nvs;
        if (nvs_open(MIMI_NVS_TG, NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, TG_LAST_CHAT_NVS_KEY, chat_id);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
}

const char *telegram_get_last_chat_id(void)
{
    return s_last_chat_id;
}

esp_err_t telegram_send_to_last_chat(const char *text)
{
    if (s_last_chat_id[0] == '\0') {
        ESP_LOGW(TAG, "Skipping Telegram send: no active or bound Telegram chat ID yet");
        return ESP_OK;
    }
#if MIMI_CLOUD_LINK_ENABLE
    if (cloud_link_is_connected()) {
        ESP_LOGI(TAG, "Routing send_to_last_chat to cloud link for chat %s", s_last_chat_id);
        return cloud_link_send_text(s_last_chat_id, text);
    }
#endif
    return telegram_send_message(s_last_chat_id, text);
}

esp_err_t telegram_send_photo(const char *chat_id, const uint8_t *jpeg_data, size_t jpeg_len, const char *caption)
{
    if (!chat_id || chat_id[0] == '\0' || !jpeg_data || jpeg_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_bot_token[0] == '\0') {
        ESP_LOGW(TAG, "Cannot send photo: no bot token");
        return ESP_ERR_INVALID_STATE;
    }

    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendPhoto", s_bot_token);

    http_resp_t resp = {
        .buf = heap_caps_calloc(1, 4096, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = 4096,
    };
    if (!resp.buf) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp.buf);
        return ESP_FAIL;
    }

    const char *boundary = "----WebKitFormBoundaryesp32s3mimi";
    char header_content_type[128];
    snprintf(header_content_type, sizeof(header_content_type), "multipart/form-data; boundary=%s", boundary);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", header_content_type);

    char body_start[512];
    int start_len = snprintf(body_start, sizeof(body_start),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"capture.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",
        boundary, chat_id, boundary);

    char body_end[256];
    int end_len = 0;
    if (caption && caption[0] != '\0') {
        end_len = snprintf(body_end, sizeof(body_end),
            "\r\n--%s\r\n"
            "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
            "%s\r\n"
            "--%s--\r\n",
            boundary, caption, boundary);
    } else {
        end_len = snprintf(body_end, sizeof(body_end),
            "\r\n--%s--\r\n",
            boundary);
    }

    int total_len = start_len + jpeg_len + end_len;
    char len_str[32];
    snprintf(len_str, sizeof(len_str), "%d", total_len);
    esp_http_client_set_header(client, "Content-Length", len_str);

    esp_err_t err = esp_http_client_open(client, total_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Uploading photo to Telegram (%d bytes total)...", total_len);
        esp_http_client_write(client, body_start, start_len);
        esp_http_client_write(client, (const char *)jpeg_data, jpeg_len);
        esp_http_client_write(client, body_end, end_len);
        
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length >= 0) {
            esp_http_client_read_response(client, resp.buf, resp.cap - 1);
            ESP_LOGI(TAG, "Telegram photo send response: %.300s", resp.buf);
        } else {
            ESP_LOGE(TAG, "Failed to read response headers");
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(resp.buf);

    return err;
}

esp_err_t telegram_send_photo_to_last_chat(const uint8_t *jpeg_data, size_t jpeg_len, const char *caption)
{
    if (!jpeg_data || jpeg_len == 0) return ESP_ERR_INVALID_ARG;
#if MIMI_CLOUD_LINK_ENABLE
    /* In hybrid mode or without token, route to backend */
    char *reply = voice_client_vision_query(jpeg_data, jpeg_len, caption ? caption : "");
    if (reply) {
        free(reply);
        return ESP_OK;
    }
#endif
    if (s_last_chat_id[0] != '\0' && s_bot_token[0] != '\0') {
        return telegram_send_photo(s_last_chat_id, jpeg_data, jpeg_len, caption);
    }
    ESP_LOGW(TAG, "Skipping Telegram photo send: no active Telegram chat ID or bot token");
    return ESP_ERR_INVALID_STATE;
}

esp_err_t telegram_send_video(const char *chat_id, const uint8_t *video_data, size_t video_len, const char *caption)
{
    if (!chat_id || chat_id[0] == '\0' || !video_data || video_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_bot_token[0] == '\0') {
        ESP_LOGW(TAG, "Cannot send video: no bot token");
        return ESP_ERR_INVALID_STATE;
    }

    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendVideo", s_bot_token);

    http_resp_t resp = {
        .buf = heap_caps_calloc(1, 4096, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = 4096,
    };
    if (!resp.buf) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 60000, /* multi-MB uploads take far longer than photos */
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp.buf);
        return ESP_FAIL;
    }

    const char *boundary = "----WebKitFormBoundaryesp32s3mimi";
    char header_content_type[128];
    snprintf(header_content_type, sizeof(header_content_type), "multipart/form-data; boundary=%s", boundary);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", header_content_type);

    char body_start[512];
    int start_len = snprintf(body_start, sizeof(body_start),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"supports_streaming\"\r\n\r\n"
        "true\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"video\"; filename=\"capture.avi\"\r\n"
        "Content-Type: video/avi\r\n\r\n",
        boundary, chat_id, boundary, boundary);

    char body_end[256];
    int end_len = 0;
    if (caption && caption[0] != '\0') {
        end_len = snprintf(body_end, sizeof(body_end),
            "\r\n--%s\r\n"
            "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
            "%s\r\n"
            "--%s--\r\n",
            boundary, caption, boundary);
    } else {
        end_len = snprintf(body_end, sizeof(body_end),
            "\r\n--%s--\r\n",
            boundary);
    }

    int total_len = start_len + video_len + end_len;
    char len_str[32];
    snprintf(len_str, sizeof(len_str), "%d", total_len);
    esp_http_client_set_header(client, "Content-Length", len_str);

    esp_err_t err = esp_http_client_open(client, total_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Uploading video to Telegram (%d bytes total)...", total_len);
        esp_http_client_write(client, body_start, start_len);

        /* Write the video in 4 KB chunks — esp_http_client_write can truncate
         * large single writes. */
        size_t sent = 0;
        while (sent < video_len && err == ESP_OK) {
            size_t chunk = video_len - sent;
            if (chunk > 4096) chunk = 4096;
            int w = esp_http_client_write(client, (const char *)(video_data + sent), chunk);
            if (w < 0) {
                ESP_LOGE(TAG, "Video write failed at %u/%u bytes", (unsigned)sent, (unsigned)video_len);
                err = ESP_FAIL;
            } else {
                sent += w;
            }
        }

        if (err == ESP_OK) {
            esp_http_client_write(client, body_end, end_len);

            int content_length = esp_http_client_fetch_headers(client);
            if (content_length >= 0) {
                esp_http_client_read_response(client, resp.buf, resp.cap - 1);
                ESP_LOGI(TAG, "Telegram video send response: %.300s", resp.buf);
            } else {
                ESP_LOGE(TAG, "Failed to read response headers");
                err = ESP_FAIL;
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(resp.buf);

    return err;
}

esp_err_t telegram_send_video_to_last_chat(const uint8_t *video_data, size_t video_len, const char *caption)
{
    if (s_last_chat_id[0] != '\0') {
        return telegram_send_video(s_last_chat_id, video_data, video_len, caption);
    }
    ESP_LOGW(TAG, "Skipping Telegram video send: no active Telegram chat ID yet");
    return ESP_ERR_INVALID_STATE;
}
