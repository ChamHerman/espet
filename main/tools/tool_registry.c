#include "tool_registry.h"
#include "mimi_config.h"
#include "sync/backend_discovery.h"
#include "display/display_status.h"
#include "cJSON.h"
#include "tools/tool_web_search.h"
#include "tools/tool_get_time.h"
#include "tools/tool_files.h"
#include "tools/tool_cron.h"
#include "tools/tool_gpio.h"
#include "tools/tool_audio.h"
#include "tools/tool_camera.h"
#include "channels/telegram/telegram_bot.h"
#include "wifi/wifi_manager.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#endif

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "tools";

#define MAX_TOOLS 128

static mimi_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;  /* cached JSON array string */

static void register_tool(const mimi_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        ESP_LOGE(TAG, "Tool registry full");
        return;
    }
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, tool->name) == 0) {
            return;
        }
    }
    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Registered tool: %s", tool->name);
}

static void build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();

    int visible_count = 0;
    for (int i = 0; i < s_tool_count; i++) {
        if (s_tools[i].hidden) {
            continue;
        }
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
        visible_count++;
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Tools JSON built (%d visible tools, %d total in registry)", visible_count, s_tool_count);
}

static esp_err_t oled_print_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "{\"error\": \"Invalid JSON\"}");
        return ESP_OK;
    }
    cJSON *text_item = cJSON_GetObjectItem(root, "text");
    cJSON *emotion_item = cJSON_GetObjectItem(root, "emotion");
    const char *emotion = cJSON_IsString(emotion_item) ? emotion_item->valuestring : NULL;

    if (emotion && emotion[0]) {
        if (strcasecmp(emotion, "happy") == 0) display_face_set_state(FACE_STATE_HAPPY);
        else if (strcasecmp(emotion, "blink") == 0) display_face_trigger_blink();
        else if (strcasecmp(emotion, "sleep") == 0) display_face_set_state(FACE_STATE_SLEEP);
        else if (strcasecmp(emotion, "think") == 0) display_face_set_state(FACE_STATE_THINK);
        else if (strcasecmp(emotion, "listen") == 0) display_face_set_state(FACE_STATE_LISTEN);
        else if (strcasecmp(emotion, "confused") == 0) display_face_set_state(FACE_STATE_CONFUSED);
        else if (strcasecmp(emotion, "idle") == 0) display_face_set_state(FACE_STATE_IDLE);
        snprintf(output, output_size, "{\"status\": \"success\", \"emotion\": \"%s\"}", emotion);
    } else if (cJSON_IsString(text_item) && text_item->valuestring[0]) {
        display_status_set(text_item->valuestring, "");
        snprintf(output, output_size, "{\"status\": \"success\", \"displayed\": \"%s\"}",
                 text_item->valuestring);
    } else {
        snprintf(output, output_size, "{\"error\": \"Missing text or emotion parameter\"}");
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t tool_take_photo_execute(const char *input_json, char *output, size_t output_size)
{
    char caption_buf[128] = "📸 Snapshot from ESPET";
    char chat_id_buf[64] = {0};

    cJSON *root = cJSON_Parse(input_json);
    if (root) {
        cJSON *cap_item = cJSON_GetObjectItem(root, "caption");
        if (cJSON_IsString(cap_item) && cap_item->valuestring[0]) {
            snprintf(caption_buf, sizeof(caption_buf), "%s", cap_item->valuestring);
        }
        cJSON *cid_item = cJSON_GetObjectItem(root, "chat_id");
        if (cJSON_IsString(cid_item) && cid_item->valuestring[0]) {
            snprintf(chat_id_buf, sizeof(chat_id_buf), "%s", cid_item->valuestring);
        }
        cJSON_Delete(root);
    }

    /* Capture photo from OV2640 camera */
    camera_fb_t *fb = tool_camera_take_picture();
    if (!fb || !fb->buf || fb->len == 0) {
        if (fb) tool_camera_return_picture(fb);
        snprintf(output, output_size, "{\"error\": \"Camera capture failed or sensor busy\"}");
        return ESP_OK;
    }

    esp_err_t err;
    if (chat_id_buf[0] != '\0') {
        err = telegram_send_photo(chat_id_buf, fb->buf, fb->len, caption_buf);
    } else {
        err = telegram_send_photo_to_last_chat(fb->buf, fb->len, caption_buf);
    }

    tool_camera_return_picture(fb);

    if (err == ESP_OK) {
        snprintf(output, output_size, "{\"status\": \"success\", \"message\": \"Photo captured successfully and displayed in chat.\", \"image_url\": \"/api/camera/last-photo\"}");
    } else {
        snprintf(output, output_size, "{\"status\": \"error\", \"message\": \"Photo captured (%u bytes) but upload failed (err=%d).\", \"code\": %d}",
                 (unsigned)(fb ? fb->len : 0), (int)err, (int)err);
    }
    return ESP_OK;
}

static esp_err_t tool_get_device_status_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        snprintf(output, output_size, "{\"error\": \"Memory allocation failed\"}");
        return ESP_OK;
    }

    /* 1. WiFi Status */
    cJSON *wifi = cJSON_CreateObject();
    bool connected = wifi_manager_is_connected();
    cJSON_AddBoolToObject(wifi, "connected", connected);
    cJSON_AddStringToObject(wifi, "ip", wifi_manager_get_ip());
    if (connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddStringToObject(wifi, "ssid", (const char *)ap_info.ssid);
            cJSON_AddNumberToObject(wifi, "rssi_dbm", ap_info.rssi);
        }
    }
    cJSON_AddItemToObject(root, "wifi", wifi);

    /* 2. Uptime */
    int64_t uptime_sec = esp_timer_get_time() / 1000000LL;
    int h = (int)(uptime_sec / 3600);
    int m = (int)((uptime_sec % 3600) / 60);
    int s = (int)(uptime_sec % 60);
    char uptime_str[64];
    snprintf(uptime_str, sizeof(uptime_str), "%dh %dm %ds", h, m, s);
    cJSON_AddStringToObject(root, "uptime", uptime_str);
    cJSON_AddNumberToObject(root, "uptime_seconds", (double)uptime_sec);

    /* 3. Memory */
    cJSON *mem = cJSON_CreateObject();
    cJSON_AddNumberToObject(mem, "free_psram_bytes", (double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(mem, "free_internal_dram_bytes", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(mem, "free_total_heap_bytes", (double)esp_get_free_heap_size());
    cJSON_AddItemToObject(root, "memory", mem);

    /* 4. SPIFFS Storage */
    size_t spiffs_total = 0, spiffs_used = 0;
    if (esp_spiffs_info(NULL, &spiffs_total, &spiffs_used) == ESP_OK) {
        cJSON *storage = cJSON_CreateObject();
        cJSON_AddNumberToObject(storage, "total_bytes", (double)spiffs_total);
        cJSON_AddNumberToObject(storage, "used_bytes", (double)spiffs_used);
        cJSON_AddNumberToObject(storage, "free_bytes", (double)(spiffs_total > spiffs_used ? spiffs_total - spiffs_used : 0));
        cJSON_AddItemToObject(root, "spiffs_storage", storage);
    }

    /* 5. CPU Temperature */
#if CONFIG_SOC_TEMP_SENSOR_SUPPORTED
    temperature_sensor_handle_t temp_handle = NULL;
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    if (temperature_sensor_install(&temp_config, &temp_handle) == ESP_OK) {
        if (temperature_sensor_enable(temp_handle) == ESP_OK) {
            float tsens_value = 0.0f;
            if (temperature_sensor_get_celsius(temp_handle, &tsens_value) == ESP_OK) {
                cJSON_AddNumberToObject(root, "cpu_temperature_celsius", (double)((int)(tsens_value * 10.0f)) / 10.0);
            }
            temperature_sensor_disable(temp_handle);
        }
        temperature_sensor_uninstall(temp_handle);
    }
#endif

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        snprintf(output, output_size, "%s", json_str);
        free(json_str);
    } else {
        snprintf(output, output_size, "{\"error\": \"Failed to serialize status JSON\"}");
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static void delayed_restart_task(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "Rebooting device now...");
    esp_restart();
}

static esp_err_t tool_restart_device_execute(const char *input_json, char *output, size_t output_size)
{
    char reason_buf[128] = "User requested restart";
    cJSON *root = cJSON_Parse(input_json);
    if (root) {
        cJSON *reason_item = cJSON_GetObjectItem(root, "reason");
        if (cJSON_IsString(reason_item) && reason_item->valuestring[0]) {
            snprintf(reason_buf, sizeof(reason_buf), "%s", reason_item->valuestring);
        }
        cJSON_Delete(root);
    }

    ESP_LOGW(TAG, "Device restart requested: %s", reason_buf);

    BaseType_t ret = xTaskCreate(delayed_restart_task, "delayed_restart", 2048, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create delayed restart task, restarting immediately");
        esp_restart();
    }

    snprintf(output, output_size, "{\"status\": \"success\", \"message\": \"Device is restarting in 1.5 seconds... Reason: %s\"}", reason_buf);
    return ESP_OK;
}

static void delayed_forget_wifi_task(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGW(TAG, "Forgetting Wi-Fi credentials and triggering BLE provisioning...");
    wifi_manager_clear_credentials();
    wifi_manager_trigger_ble_prov();
    vTaskDelete(NULL);
}

static esp_err_t tool_forget_wifi_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    ESP_LOGW(TAG, "forget_wifi tool invoked");
    display_status_set("WiFi Reset", "Entering BLE...");

    BaseType_t ret = xTaskCreate(delayed_forget_wifi_task, "forget_wifi_task", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create delayed forget_wifi task, executing immediately");
        wifi_manager_clear_credentials();
        wifi_manager_trigger_ble_prov();
    }

    snprintf(output, output_size,
             "{\"status\": \"success\", \"message\": \"Wi-Fi credentials have been erased from NVS. The device is restarting into BLE Provisioning mode in 1.5 seconds. Please inform the user that Wi-Fi has been forgotten and to reconnect via BLE setup.\"}");
    return ESP_OK;
}

esp_err_t tool_registry_init(void)
{
    s_tool_count = 0;

    /* Register web_search */
    tool_web_search_init();

    mimi_tool_t ws = {
        .name = "web_search",
        .description = "Search the web for current information via Tavily (preferred) or Brave when configured.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"The search query\"}},"
            "\"required\":[\"query\"]}",
        .execute = tool_web_search_execute,
    };
    register_tool(&ws);

    /* Register get_current_time */
    mimi_tool_t gt = {
        .name = "get_current_time",
        .description = "Get the current date and time. Also sets the system clock. Call this when you need to know what time or date it is.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_get_time_execute,
    };
    register_tool(&gt);

    /* Register read_file */
    mimi_tool_t rf = {
        .name = "read_file",
        .description = "Read a file from SPIFFS storage. Path must start with " MIMI_SPIFFS_BASE "/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"}},"
            "\"required\":[\"path\"]}",
        .execute = tool_read_file_execute,
    };
    register_tool(&rf);

    /* Register write_file */
    mimi_tool_t wf = {
        .name = "write_file",
        .description = "Write or overwrite a file on SPIFFS storage. Path must start with " MIMI_SPIFFS_BASE "/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"},"
            "\"content\":{\"type\":\"string\",\"description\":\"File content to write\"}},"
            "\"required\":[\"path\",\"content\"]}",
        .execute = tool_write_file_execute,
    };
    register_tool(&wf);

    /* Register edit_file */
    mimi_tool_t ef = {
        .name = "edit_file",
        .description = "Find and replace text in a file on SPIFFS. Replaces first occurrence of old_string with new_string.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"},"
            "\"old_string\":{\"type\":\"string\",\"description\":\"Text to find\"},"
            "\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"}},"
            "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
        .execute = tool_edit_file_execute,
    };
    register_tool(&ef);

    /* Register list_dir */
    mimi_tool_t ld = {
        .name = "list_dir",
        .description = "List files on SPIFFS storage, optionally filtered by path prefix.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"Optional path prefix filter, e.g. " MIMI_SPIFFS_BASE "/memory/\"}},"
            "\"required\":[]}",
        .execute = tool_list_dir_execute,
    };
    register_tool(&ld);

    /* Register cron_add */
    mimi_tool_t ca = {
        .name = "cron_add",
        .description = "Schedule a recurring or one-shot task. The message will trigger an agent turn when the job fires.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"name\":{\"type\":\"string\",\"description\":\"Short name for the job\"},"
            "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring interval or 'at' for one-shot at a unix timestamp\"},"
            "\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval in seconds (required for 'every')\"},"
            "\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp to fire at (required for 'at')\"},"
            "\"message\":{\"type\":\"string\",\"description\":\"Message to inject when the job fires, triggering an agent turn\"},"
            "\"channel\":{\"type\":\"string\",\"description\":\"Optional reply channel (e.g. 'telegram'). If omitted, current turn channel is used when available\"},"
            "\"chat_id\":{\"type\":\"string\",\"description\":\"Optional reply chat_id. Required when channel='telegram'. If omitted during a Telegram turn, current chat_id is used\"}"
            "},"
            "\"required\":[\"name\",\"schedule_type\",\"message\"]}",
        .execute = tool_cron_add_execute,
    };
    register_tool(&ca);

    /* Register cron_list */
    mimi_tool_t cl = {
        .name = "cron_list",
        .description = "List all scheduled cron jobs with their status, schedule, and IDs.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_cron_list_execute,
    };
    register_tool(&cl);

    /* Register cron_remove */
    mimi_tool_t cr = {
        .name = "cron_remove",
        .description = "Remove a scheduled cron job by its ID.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"The 8-character job ID to remove\"}},"
            "\"required\":[\"job_id\"]}",
        .execute = tool_cron_remove_execute,
    };
    register_tool(&cr);

    /* Register oled_print — display text or trigger facial emotion on OLED */
    mimi_tool_t op = {
        .name = "oled_print",
        .description = "Display text on OLED or set robot eye emotion: happy, blink, sleep, think, listen, confused, idle.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"text\":{\"type\":\"string\",\"description\":\"Text to display (optional if emotion given)\"},"
            "\"emotion\":{\"type\":\"string\",\"description\":\"Face emotion: happy, blink, sleep, think, listen, confused, idle\"}"
            "}}",
        .execute = oled_print_execute,
    };
    register_tool(&op);

    /* Register GPIO tools */
    tool_gpio_init();

    mimi_tool_t gw = {
        .name = "gpio_write",
        .description = "Set a GPIO pin HIGH (1) or LOW (0). Fixed hardware pins: GPIO 42 = WHITE LED.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number (e.g. 42 for WHITE LED)\"},"
            "\"state\":{\"type\":\"integer\",\"description\":\"1 for HIGH (ON), 0 for LOW (OFF)\"}},"
            "\"required\":[\"pin\",\"state\"]}",
        .execute = tool_gpio_write_execute,
    };
    register_tool(&gw);

    mimi_tool_t gr = {
        .name = "gpio_read",
        .description = "Read a GPIO pin state (returns 1 for HIGH, 0 for LOW). Fixed hardware pins: GPIO 41 = GREEN Button (GPIO 38/39 are shared I2S audio clocks).",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number (e.g. 41 for GREEN Button)\"}},"
            "\"required\":[\"pin\"]}",
        .execute = tool_gpio_read_execute,
    };
    register_tool(&gr);

    mimi_tool_t ga = {
        .name = "gpio_read_all",
        .description = "Read all allowed GPIO pin states at once (GPIO 1, 2, 21, 38, 39, 40, 41, 42, 47).",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_gpio_read_all_execute,
        .hidden = true, /* Diagnostic tool: available for serial REPL, hidden from LLM prompt */
    };
    register_tool(&ga);


    /* Register Audio diagnostic tools (available via serial REPL, hidden from LLM prompt) */
    tool_audio_init();
    audio_play_tone_tool.hidden = true;
    audio_record_test_tool.hidden = true;
    audio_spktest_tool.hidden = true;
    register_tool(&audio_play_tone_tool);
    register_tool(&audio_record_test_tool);
    register_tool(&audio_spktest_tool);

    /* Register take_photo */
    mimi_tool_t tp = {
        .name = "take_photo",
        .description = "Capture a real-time photo with the device camera and send it to the user on Telegram. Call this when the user asks to take a photo, snap a picture, or show what you see.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"caption\":{\"type\":\"string\",\"description\":\"Optional message or caption to accompany the photo\"},"
            "\"chat_id\":{\"type\":\"string\",\"description\":\"Optional Telegram chat_id (uses current chat if omitted)\"}"
            "},"
            "\"required\":[]}",
        .execute = tool_take_photo_execute,
    };
    register_tool(&tp);

    /* Register get_device_status */
    mimi_tool_t gds = {
        .name = "get_device_status",
        .description = "Get device diagnostic health vitals: WiFi connection status, SSID, signal strength (RSSI in dBm), IP address, uptime, free RAM/PSRAM, flash storage, and CPU temperature. Call this when user asks about device health, battery/power, signal, temperature, or status.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_get_device_status_execute,
    };
    register_tool(&gds);

    /* Register restart_device */
    mimi_tool_t rd = {
        .name = "restart_device",
        .description = "Restart/reboot the ESP32-S3 microcontroller device. Performs a clean restart after a 1.5 second delay so the confirmation message is sent first. Use when user asks to reboot or restart the device.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"reason\":{\"type\":\"string\",\"description\":\"Optional reason for restarting\"}},"
            "\"required\":[]}",
        .execute = tool_restart_device_execute,
    };
    register_tool(&rd);

    /* Register forget_wifi */
    mimi_tool_t fw = {
        .name = "forget_wifi",
        .description = "Forget/reset the stored Wi-Fi credentials from NVS and restart the device into BLE Provisioning mode. Call this whenever the user asks to forget Wi-Fi, reset Wi-Fi, clear Wi-Fi password, or reconfigure Wi-Fi setup.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_forget_wifi_execute,
    };
    register_tool(&fw);

    build_tools_json();

    ESP_LOGI(TAG, "Tool registry initialized");
    return ESP_OK;
}

const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

/* ---------------------------------------------------------------------------
 * Remote Tool Execution via Backend Server (/api/mcp/execute)
 * --------------------------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t len;
    size_t max_len;
} http_resp_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        http_resp_buf_t *r = (http_resp_buf_t *)evt->user_data;
        if (r && r->buf && evt->data_len > 0) {
            size_t copy_len = evt->data_len;
            if (r->len + copy_len >= r->max_len) {
                copy_len = r->max_len - r->len - 1;
            }
            if (copy_len > 0) {
                memcpy(r->buf + r->len, evt->data, copy_len);
                r->len += copy_len;
                r->buf[r->len] = '\0';
            }
        }
    }
    return ESP_OK;
}

static esp_err_t tool_remote_execute(const char *name, const char *input_json,
                                      char *output, size_t output_size)
{
    ESP_LOGI(TAG, "Dispatching remote tool '%s' to backend...", name);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "tool_name", name);

    cJSON *args = cJSON_Parse(input_json);
    if (!args) {
        args = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(payload, "arguments", args);

    char *post_data = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (!post_data) {
        snprintf(output, output_size, "Error: failed to build JSON payload");
        return ESP_ERR_NO_MEM;
    }

    char *response_buf = calloc(1, 4096);
    if (!response_buf) {
        free(post_data);
        snprintf(output, output_size, "Error: out of memory");
        return ESP_ERR_NO_MEM;
    }

    http_resp_buf_t resp_ctx = {
        .buf = response_buf,
        .len = 0,
        .max_len = 4096,
    };

    char auth_hdr[128];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", MIMI_BACKEND_API_KEY);

    char url[256];
    char base[96];
    backend_discovery_base_url(base, sizeof(base));
    snprintf(url, sizeof(url), "%s/api/mcp/execute", base);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_hdr);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(post_data);

    if (err != ESP_OK || status_code != 200) {
        ESP_LOGE(TAG, "Backend HTTP failed: err=%d status=%d", err, status_code);
        snprintf(output, output_size, "Error: backend server returned HTTP %d (err %d)", status_code, err);
        free(response_buf);
        return ESP_FAIL;
    }

    /* Parse JSON response: {"success": true, "result": {...}, "error": "..."} */
    cJSON *resp_json = cJSON_Parse(response_buf);
    if (!resp_json) {
        snprintf(output, output_size, "%s", response_buf);
        free(response_buf);
        return ESP_OK;
    }
    free(response_buf);

    cJSON *error = cJSON_GetObjectItem(resp_json, "error");
    cJSON *result = cJSON_GetObjectItem(resp_json, "result");

    if (error && error->valuestring) {
        snprintf(output, output_size, "Error: %s", error->valuestring);
    } else if (result) {
        char *printed = cJSON_PrintUnformatted(result);
        if (printed) {
            snprintf(output, output_size, "%s", printed);
            free(printed);
        } else {
            snprintf(output, output_size, "Success");
        }
    } else {
        snprintf(output, output_size, "Tool executed successfully.");
    }

    cJSON_Delete(resp_json);
    return ESP_OK;
}

static void *psram_malloc(size_t sz) {
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!p) p = malloc(sz);
    return p;
}

static void psram_free(void *ptr) {
    free(ptr);
}

esp_err_t tool_registry_fetch_remote_tools(void)
{
    char base[96];
    backend_discovery_base_url(base, sizeof(base));
    ESP_LOGI(TAG, "Fetching remote tools from backend (%s/api/tools)...", base);

    char *response_buf = heap_caps_calloc(1, 262144, MALLOC_CAP_SPIRAM);
    if (!response_buf) {
        response_buf = calloc(1, 262144);
    }
    if (!response_buf) {
        ESP_LOGE(TAG, "Failed to allocate buffer for remote tools");
        return ESP_ERR_NO_MEM;
    }

    http_resp_buf_t resp_ctx = {
        .buf = response_buf,
        .len = 0,
        .max_len = 262144,
    };

    char auth_hdr[128];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", MIMI_BACKEND_API_KEY);

    char url[256];
    snprintf(url, sizeof(url), "%s/api/tools", base);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Authorization", auth_hdr);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status_code != 200) {
        ESP_LOGW(TAG, "Failed to fetch remote tools: HTTP %d (err %d)", status_code, err);
        free(response_buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Fetched remote tools response: len=%u bytes", (unsigned)resp_ctx.len);

    cJSON_Hooks hooks = {
        .malloc_fn = psram_malloc,
        .free_fn = psram_free,
    };
    cJSON_InitHooks(&hooks);

    cJSON *resp_json = cJSON_Parse(response_buf);
    free(response_buf);
    if (!resp_json) {
        ESP_LOGE(TAG, "Invalid JSON from /api/tools (len=%u, err_at=%.40s)",
                 (unsigned)resp_ctx.len,
                 cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "null");
        cJSON_InitHooks(NULL);
        return ESP_FAIL;
    }

    cJSON *tools_arr = cJSON_GetObjectItem(resp_json, "tools");
    if (cJSON_IsArray(tools_arr)) {
        int added = 0;
        int size = cJSON_GetArraySize(tools_arr);
        for (int i = 0; i < size; i++) {
            cJSON *t = cJSON_GetArrayItem(tools_arr, i);
            if (!t) continue;
            cJSON *name = cJSON_GetObjectItem(t, "name");
            cJSON *desc = cJSON_GetObjectItem(t, "description");
            cJSON *params = cJSON_GetObjectItem(t, "parameters");
            if (!params) {
                params = cJSON_GetObjectItem(t, "input_schema");
            }

            if (name && name->valuestring) {
                char *schema_str = NULL;
                if (params) {
                    schema_str = cJSON_PrintUnformatted(params);
                }
                if (!schema_str) {
                    schema_str = strdup("{\"type\":\"object\",\"properties\":{}}");
                }

                if (s_tool_count < MAX_TOOLS) {
                    mimi_tool_t rt = {
                        .name = strdup(name->valuestring),
                        .description = desc && desc->valuestring ? strdup(desc->valuestring) : "Remote MCP tool",
                        .input_schema_json = schema_str,
                        .execute = NULL,
                    };
                    register_tool(&rt);
                    added++;
                }
            }
        }
        if (added > 0) {
            build_tools_json();
            ESP_LOGI(TAG, "Added %d remote tools from backend", added);
        }
    }

    cJSON_Delete(resp_json);
    cJSON_InitHooks(NULL);
    return ESP_OK;
}

esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size)
{

    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            ESP_LOGI(TAG, "Executing tool: %s", name);
            display_status_set("Tool:", name);
            if (s_tools[i].execute) {
                ret = s_tools[i].execute(input_json, output, output_size);
            } else {
                ret = tool_remote_execute(name, input_json, output, output_size);
            }
            display_status_set(ret == ESP_OK ? "Done:" : "Tool failed:", name);
            return ret;
        }
    }

    /* Fallback: try remote execution if tool name wasn't registered locally */
    ESP_LOGI(TAG, "Tool '%s' not registered locally, trying remote execution...", name);
    display_status_set("Tool:", name);
    ret = tool_remote_execute(name, input_json, output, output_size);
    display_status_set(ret == ESP_OK ? "Done:" : "Tool failed:", name);
    return ret;
}

