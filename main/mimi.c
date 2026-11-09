#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "mimi_config.h"
#include "bus/message_bus.h"
#include "wifi/wifi_manager.h"
#include "channels/telegram/telegram_bot.h"
#include "channels/cloud_link/cloud_link.h"
#include "channels/feishu/feishu_bot.h"
#include "llm/llm_proxy.h"
#include "agent/agent_loop.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "gateway/ws_server.h"
#include "cli/serial_cli.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "cron/cron_service.h"
#include "heartbeat/heartbeat.h"
#include "sync/config_sync.h"
#include "sync/backend_discovery.h"
#include "display/display_status.h"
#include "skills/skill_loader.h"
#include "onboard/wifi_onboard_ble.h"
#include "tools/button_handler.h"
#include "tools/tool_audio.h"
#include "voice/voice_client.h"
#include "tools/tool_camera.h"

static const char *TAG = "mimi";

extern char s_last_chat_id[64]; // Quick hack to get chat id for camera if needed, or we just rely on telegram_send_to_last_chat logic inside telegram_bot.c
// But telegram_send_photo needs chat_id. Let's just expose a getter in telegram_bot.h ideally.
// For now, we'll just log if chat_id isn't known, or we'll send a system event.

static void on_white_button_short_press(void)
{
    ESP_LOGI(TAG, "[WHITE BUTTON] Short Press: Capturing Image!");
    camera_fb_t *fb = tool_camera_take_picture();
    if (fb) {
        ESP_LOGI(TAG, "Image Captured! Size: %zu. Sending to Telegram...", fb->len);
        
        esp_err_t err = telegram_send_photo_to_last_chat(fb->buf, fb->len, "What do you want to know about this picture?");
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Photo sent successfully!");
        } else {
            ESP_LOGE(TAG, "Failed to send photo: %d", err);
        }
        
        tool_camera_return_picture(fb);
    }
}

static void on_white_button_long_press(bool is_starting)
{
    if (is_starting) {
        ESP_LOGI(TAG, "[WHITE BUTTON] Long Press: Starting Video Capture");
        tool_camera_video_start();
    } else {
        ESP_LOGI(TAG, "[WHITE BUTTON] Long Press Released: Stopping Video Capture");
        uint8_t *avi = NULL;
        size_t avi_len = 0;
        esp_err_t verr = tool_camera_video_stop_and_get(&avi, &avi_len);
        if (verr != ESP_OK || !avi) {
            ESP_LOGW(TAG, "Video capture produced nothing (err=%d)", verr);
            return;
        }

        ESP_LOGI(TAG, "Video captured (%u bytes). Sending to Telegram...",
                 (unsigned)avi_len);
        esp_err_t err = telegram_send_video_to_last_chat(
            avi, avi_len, "What do you want to know about this video?");
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Video sent successfully!");
        } else if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "No Telegram chat known yet — send the bot a "
                          "message once, then hold the button again");
        } else {
            ESP_LOGE(TAG, "Failed to send video: %d", err);
        }
        tool_camera_video_release();
    }
}

/* GPIO 41 camera button: short press = snapshot to Telegram,
 * hold >= 600 ms = video capture start/stop. */

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MIMI_SPIFFS_BASE,
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", (int)total, (int)used);

    return ESP_OK;
}

/* Replies must not die in a network outage window: the bot can hit
 * multi-minute stretches where every outbound TLS connection fails
 * (router/ISP drops) — retry instead of dropping the message. */
#define TG_SEND_RETRIES   6
#define TG_SEND_RETRY_MS  5000

/* Outbound dispatch task: reads from outbound queue and routes to channels */
/* Remote MCP tool discovery: retries in the background until the backend is
 * actually reachable. The boot-time fetch runs before mDNS has resolved, so it
 * hits the stale compiled-in IP and (before this) never retried — the device
 * ended up with zero remote tools. Re-fetch is idempotent (register_tool skips
 * duplicates), so a successful later pass simply adds what was missing. */
static void remote_tools_fetch_task(void *arg)
{
    for (int attempt = 1; attempt <= 10; attempt++) {
        if (tool_registry_fetch_remote_tools() == ESP_OK) {
            ESP_LOGI(TAG, "Remote tools ready (attempt %d)", attempt);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    vTaskDelete(NULL);
}
/* ponytail: skip status and error fallback strings so TTS only speaks true assistant answers */
static bool should_speak_reply(const char *content)
{
    if (!content || content[0] == '\0') return false;
    if (strncmp(content, "ESPeting", 8) == 0) return false;
    if (strstr(content, "Sorry, I encountered an error") != NULL) return false;
    return true;
}


static void outbound_dispatch_task(void *arg)
{
    ESP_LOGI(TAG, "Outbound dispatch started");

    while (1) {
        mimi_msg_t msg;
        if (message_bus_pop_outbound(&msg, UINT32_MAX) != ESP_OK) continue;

        ESP_LOGI(TAG, "Dispatching response to %s:%s", msg.channel, msg.chat_id);

        if (strcmp(msg.channel, MIMI_CHAN_TELEGRAM) == 0) {
            if (msg.chat_id[0] == '\0') {
                ESP_LOGW(TAG, "Skipping Telegram send: chat_id is empty");
            } else {
                esp_err_t send_err = ESP_FAIL;
                for (int attempt = 1; attempt <= TG_SEND_RETRIES; attempt++) {
                    send_err = telegram_send_message(msg.chat_id, msg.content);
                    if (send_err == ESP_OK) {
                        break;
                    }
                    if (attempt < TG_SEND_RETRIES) {
                        ESP_LOGW(TAG, "Telegram send failed (attempt %d/%d) — "
                                      "retrying in %d s", attempt,
                                 TG_SEND_RETRIES, TG_SEND_RETRY_MS / 1000);
                        vTaskDelay(pdMS_TO_TICKS(TG_SEND_RETRY_MS));
                    }
                }
                if (send_err != ESP_OK) {
                    ESP_LOGE(TAG, "Telegram send failed for %s after %d attempts: %s",
                             msg.chat_id, TG_SEND_RETRIES,
                             esp_err_to_name(send_err));
                } else {
                    ESP_LOGI(TAG, "Telegram send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
                    /* Final agent replies are spoken too (user request):
                     * every text answer also comes out of the speaker. */
                    if (should_speak_reply(msg.content)) {
                        voice_client_speak(msg.content);
                    }
                }
            }
#if MIMI_CLOUD_LINK_ENABLE
        } else if (strcmp(msg.channel, MIMI_CHAN_CLOUD) == 0) {
            /* Hybrid mode: replies to cloud-routed Telegram messages go back
             * through the gateway. Same retry discipline as Telegram sends. */
            if (msg.chat_id[0] == '\0') {
                ESP_LOGW(TAG, "Skipping cloud send: chat_id is empty");
            } else {
                esp_err_t send_err = ESP_FAIL;
                for (int attempt = 1; attempt <= TG_SEND_RETRIES; attempt++) {
                    send_err = cloud_link_send_text(msg.chat_id, msg.content);
                    if (send_err == ESP_OK) {
                        break;
                    }
                    if (attempt < TG_SEND_RETRIES) {
                        ESP_LOGW(TAG, "Cloud send failed (attempt %d/%d) — "
                                      "retrying in %d s", attempt,
                                 TG_SEND_RETRIES, TG_SEND_RETRY_MS / 1000);
                        vTaskDelay(pdMS_TO_TICKS(TG_SEND_RETRY_MS));
                    }
                }
                if (send_err != ESP_OK) {
                    ESP_LOGE(TAG, "Cloud send failed for %s after %d attempts: %s",
                             msg.chat_id, TG_SEND_RETRIES,
                             esp_err_to_name(send_err));
                } else {
                    ESP_LOGI(TAG, "Cloud send success for %s (%d bytes)",
                             msg.chat_id, (int)strlen(msg.content));
                    /* Final agent replies are spoken too (user request) */
                    if (should_speak_reply(msg.content)) {
                        voice_client_speak(msg.content);
                    }
                }
            }
#else
        } else if (strcmp(msg.channel, MIMI_CHAN_FEISHU) == 0) {
#endif
            esp_err_t send_err = feishu_send_message(msg.chat_id, msg.content);
            if (send_err != ESP_OK) {
                ESP_LOGE(TAG, "Feishu send failed for %s: %s", msg.chat_id, esp_err_to_name(send_err));
            } else {
                ESP_LOGI(TAG, "Feishu send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_WEBSOCKET) == 0) {
            esp_err_t ws_err = ws_server_send(msg.chat_id, msg.content);
            if (ws_err != ESP_OK) {
                ESP_LOGW(TAG, "WS send failed for %s: %s", msg.chat_id, esp_err_to_name(ws_err));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_SYSTEM) == 0) {
            ESP_LOGI(TAG, "System message [%s]: %.128s", msg.chat_id, msg.content);
            /* Voice pipeline output: bridge to Telegram chat so user sees output
             * while speaker is unavailable/untested, and speak through MAX98357A speaker. */
            if (strcmp(msg.chat_id, "voice") == 0) {
                size_t prefix_len = strlen("🤖 ESPET: ");
                size_t content_len = msg.content ? strlen(msg.content) : 0;
                char *tlg_msg = (char *)malloc(prefix_len + content_len + 1);
                if (tlg_msg) {
                    snprintf(tlg_msg, prefix_len + content_len + 1, "🤖 ESPET: %s", msg.content ? msg.content : "");
                    telegram_send_to_last_chat(tlg_msg);
                    free(tlg_msg);
                } else if (msg.content) {
                    telegram_send_to_last_chat(msg.content);
                }
                if (msg.content) {
                    voice_client_speak(msg.content);
                }
            }
        } else {
            ESP_LOGW(TAG, "Unknown channel: %s", msg.channel);
        }

        free(msg.content);
    }
}

void app_main(void)
{
    /* Silence noisy components */
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESPET - ESP32-S3 AI Agent");
    ESP_LOGI(TAG, "========================================");
    display_status_init();
    display_status_set("Starting...", "");

    /* Print memory info */
    ESP_LOGI(TAG, "Internal free: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free:    %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Phase 1: Core infrastructure */
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(init_spiffs());

    /* Initialize subsystems */
    ESP_ERROR_CHECK(message_bus_init());
    ESP_ERROR_CHECK(memory_store_init());
    ESP_ERROR_CHECK(skill_loader_init());
    ESP_ERROR_CHECK(session_mgr_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_proxy_init());
    ESP_ERROR_CHECK(telegram_bot_init());
    ESP_ERROR_CHECK(feishu_bot_init());
    ESP_ERROR_CHECK(llm_proxy_init());
    ESP_ERROR_CHECK(tool_registry_init());
    ESP_ERROR_CHECK(tool_camera_init());
    ESP_ERROR_CHECK(button_handler_init(on_white_button_short_press, on_white_button_long_press));
    ESP_ERROR_CHECK(cron_service_init());
    ESP_ERROR_CHECK(heartbeat_init());
    ESP_ERROR_CHECK(config_sync_init());
    ESP_ERROR_CHECK(agent_loop_init());

    /* Start Serial CLI first (works without WiFi) */
    ESP_ERROR_CHECK(serial_cli_init());

    /* Check if runtime connection loss requested immediate BLE onboarding */
    if (wifi_manager_is_ble_prov_requested()) {
        ESP_LOGW(TAG, "Wi-Fi connection lost at runtime. Entering BLE onboarding mode...");
        display_status_set("WiFi Setup (BLE)", "PROV_ESPET");
        wifi_onboard_ble_start();
        return;
    }

    /* Start WiFi */
    esp_err_t wifi_err = wifi_manager_start();
    bool wifi_ok = false;
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Scanning nearby APs on boot...");
        wifi_manager_scan_and_print();
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        if (wifi_manager_wait_connected(30000) == ESP_OK) {
            wifi_ok = true;
            ESP_LOGI(TAG, "WiFi connected: %s", wifi_manager_get_ip());
            /* Fetch remote MCP tools from backend server */
            tool_registry_fetch_remote_tools();
        } else {
            ESP_LOGW(TAG, "WiFi connection timeout");
        }
    } else {
        ESP_LOGW(TAG, "No WiFi credentials configured");
    }

    if (!wifi_ok) {
        ESP_LOGW(TAG, "Entering WiFi BLE onboarding mode...");
        wifi_onboard_ble_start();  /* blocks, restarts on success */
        return;  /* unreachable */
    }

    {
        /* Outbound dispatch task should start first to avoid dropping early replies. */
        ESP_ERROR_CHECK((xTaskCreatePinnedToCore(
            outbound_dispatch_task, "outbound",
            MIMI_OUTBOUND_STACK, NULL,
            MIMI_OUTBOUND_PRIO, NULL, MIMI_OUTBOUND_CORE) == pdPASS)
            ? ESP_OK : ESP_FAIL);

        /* Start network-dependent services */
        ESP_ERROR_CHECK(agent_loop_start());
#if MIMI_CLOUD_LINK_ENABLE
        /* Hybrid mode: the backend owns Telegram polling; the device keeps
         * one persistent WS to it. telegram_bot polling must NOT start
         * (one poller per bot token, and replies route via channel "cloud"). */
        ESP_ERROR_CHECK(cloud_link_start());
#else
        ESP_ERROR_CHECK(telegram_bot_start());
#endif
        ESP_ERROR_CHECK(feishu_bot_start());
        cron_service_start();
        heartbeat_start();
        ESP_ERROR_CHECK(ws_server_start());
        config_sync_start();

        /* Hands-free wake word listener ("Hi ESP" -> Yes? -> command -> speaker) */
        esp_err_t v_err = voice_client_start();
        if (v_err != ESP_OK) {
            ESP_LOGW(TAG, "voice wake listener not started: %s", esp_err_to_name(v_err));
        }

        /* mDNS resolve of backend URL — deliberately LAST so the internal-RAM
         * mDNS task doesn't starve boot-time task creation; falls back to the
         * compile-time IP until discovery succeeds. */
        backend_discovery_start();

        /* The boot-time fetch above used the compiled-in IP and likely failed;
         * retry in the background once mDNS has found the real address. */
        xTaskCreate(remote_tools_fetch_task, "tools_fetch", 8192, NULL,
                    tskIDLE_PRIORITY + 2, NULL);

        ESP_LOGI(TAG, "All services started!");
        display_status_idle();   /* shows "Hi ESP to wake" */
    }

    ESP_LOGI(TAG, "ESPET ready. Type 'help' for CLI commands.");
}
