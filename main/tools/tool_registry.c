#include "tool_registry.h"
#include "mimi_config.h"
#include "tools/tool_web_search.h"
#include "tools/tool_get_time.h"
#include "tools/tool_files.h"
#include "tools/tool_cron.h"
#include "tools/tool_gpio.h"
#include "tools/tool_oled.h"
#include "tools/tool_audio.h"

#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "tools";

#define MAX_TOOLS 32

static mimi_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;  /* cached JSON array string */

static void register_tool(const mimi_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        ESP_LOGE(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Registered tool: %s", tool->name);
}

static void build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Tools JSON built (%d tools)", s_tool_count);
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

    /* Register GPIO tools */
    tool_gpio_init();

    mimi_tool_t gw = {
        .name = "gpio_write",
        .description = "Set a GPIO pin HIGH (1) or LOW (0). Fixed hardware pins: GPIO 40 = WHITE LED.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number (e.g. 40 for WHITE LED)\"},"
            "\"state\":{\"type\":\"integer\",\"description\":\"1 for HIGH (ON), 0 for LOW (OFF)\"}},"
            "\"required\":[\"pin\",\"state\"]}",
        .execute = tool_gpio_write_execute,
    };
    register_tool(&gw);

    mimi_tool_t gr = {
        .name = "gpio_read",
        .description = "Read a GPIO pin state (returns 1 for HIGH, 0 for LOW). Fixed hardware pins: GPIO 39 = GREEN Button, GPIO 38 = WHITE Button.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number (e.g. 39 for GREEN Button, 38 for WHITE Button)\"}},"
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
    };
    register_tool(&ga);

    /* Register OLED tool */
    register_tool(&oled_tool);

    /* Register Audio tools */
    tool_audio_init();
    register_tool(&audio_play_tone_tool);
    register_tool(&audio_record_test_tool);

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
    snprintf(url, sizeof(url), "%s/api/mcp/execute", MIMI_BACKEND_URL);

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

esp_err_t tool_registry_fetch_remote_tools(void)
{
    ESP_LOGI(TAG, "Fetching remote tools from backend (%s/api/tools)...", MIMI_BACKEND_URL);

    char *response_buf = calloc(1, 8192);
    if (!response_buf) {
        ESP_LOGE(TAG, "Failed to allocate buffer for remote tools");
        return ESP_ERR_NO_MEM;
    }

    http_resp_buf_t resp_ctx = {
        .buf = response_buf,
        .len = 0,
        .max_len = 8192,
    };

    char auth_hdr[128];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", MIMI_BACKEND_API_KEY);

    char url[256];
    snprintf(url, sizeof(url), "%s/api/tools", MIMI_BACKEND_URL);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 10000,
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

    cJSON *resp_json = cJSON_Parse(response_buf);
    free(response_buf);
    if (!resp_json) {
        ESP_LOGW(TAG, "Invalid JSON from /api/tools");
        return ESP_FAIL;
    }

    cJSON *tools_arr = cJSON_GetObjectItem(resp_json, "tools");
    if (cJSON_IsArray(tools_arr)) {
        int added = 0;
        int count = cJSON_GetArraySize(tools_arr);
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(tools_arr, i);
            cJSON *name = cJSON_GetObjectItem(item, "name");
            cJSON *desc = cJSON_GetObjectItem(item, "description");
            cJSON *schema = cJSON_GetObjectItem(item, "input_schema");

            if (name && name->valuestring) {
                bool exists = false;
                for (int j = 0; j < s_tool_count; j++) {
                    if (strcmp(s_tools[j].name, name->valuestring) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists && s_tool_count < MAX_TOOLS) {
                    char *schema_str = schema ? cJSON_PrintUnformatted(schema) : strdup("{}");
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
    return ESP_OK;
}

esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            ESP_LOGI(TAG, "Executing tool: %s", name);
            if (s_tools[i].execute) {
                return s_tools[i].execute(input_json, output, output_size);
            } else {
                return tool_remote_execute(name, input_json, output, output_size);
            }
        }
    }

    /* Fallback: try remote execution if tool name wasn't registered locally */
    ESP_LOGI(TAG, "Tool '%s' not registered locally, trying remote execution...", name);
    return tool_remote_execute(name, input_json, output, output_size);
}

