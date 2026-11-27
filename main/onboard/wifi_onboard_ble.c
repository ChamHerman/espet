#include "wifi_onboard_ble.h"
#include "sdkconfig.h"

#if !CONFIG_BT_ENABLED
#error "CONFIG_BT_ENABLED must be enabled in sdkconfig. BLE Wi-Fi provisioning is mandatory for onboarding."
#endif

#include "wifi/wifi_manager.h"
#include "display/display_status.h"
#include "mimi_config.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sync/backend_discovery.h"
#include "cJSON.h"

static const char *TAG = "onboard_ble";

static char s_pending_ssid[33] = {0};
static char s_pending_pass[65] = {0};

static void prov_event_handler(void *user_data, wifi_prov_cb_event_t event, void *event_data)
{
    switch (event) {
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "Provisioning started");
        break;
    case WIFI_PROV_CRED_RECV: {
        wifi_sta_config_t *sta_cfg = (wifi_sta_config_t *)event_data;
        ESP_LOGI(TAG, "Received Wi-Fi credentials for SSID: %s", (const char *)sta_cfg->ssid);
        strncpy(s_pending_ssid, (const char *)sta_cfg->ssid, sizeof(s_pending_ssid) - 1);
        strncpy(s_pending_pass, (const char *)sta_cfg->password, sizeof(s_pending_pass) - 1);
        break;
    }
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "Provisioning successful!");
        if (s_pending_ssid[0] != '\0') {
            wifi_manager_set_credentials(s_pending_ssid, s_pending_pass);
        }
        display_status_set("WiFi Connected", "Saving...");
        break;
    case WIFI_PROV_CRED_FAIL: {
        wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
        ESP_LOGE(TAG, "Provisioning failed! Reason: %s",
                 (reason && *reason == WIFI_PROV_STA_AUTH_ERROR) ? "Auth error" : "AP not found");
        display_status_set("WiFi Failed", "Check Password");
        break;
    }
    case WIFI_PROV_END:
        ESP_LOGI(TAG, "Provisioning ended");
        break;
    default:
        break;
    }
}

static esp_err_t custom_config_prov_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                            uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf && inlen > 0) {
        ESP_LOGI(TAG, "Received custom config: %.*s", (int)inlen, (const char *)inbuf);
        cJSON *root = cJSON_ParseWithLength((const char *)inbuf, inlen);
        if (root) {
            cJSON *item = cJSON_GetObjectItem(root, "backend_url");
            if (cJSON_IsString(item) && item->valuestring) {
                const char *url = item->valuestring;
                nvs_handle_t nvs;
                if (url[0] != '\0') {
                    if (backend_discovery_is_loopback_url(url)) {
                        ESP_LOGW(TAG, "Rejecting loopback backend URL '%s' (localhost refers to ESP32 itself)", url);
                    } else if (strlen(url) < 96 && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0)) {
                        if (nvs_open(MIMI_NVS_BACKEND, NVS_READWRITE, &nvs) == ESP_OK) {
                            nvs_set_str(nvs, MIMI_NVS_KEY_BACKEND_URL, url);
                            nvs_commit(nvs);
                            nvs_close(nvs);
                            backend_discovery_set_url(url);
                            ESP_LOGI(TAG, "Saved backend URL to NVS: %s", url);
                        }
                    } else {
                        ESP_LOGW(TAG, "Invalid backend URL format: %s", url);
                    }
                } else {
                    if (nvs_open(MIMI_NVS_BACKEND, NVS_READWRITE, &nvs) == ESP_OK) {
                        nvs_erase_key(nvs, MIMI_NVS_KEY_BACKEND_URL);
                        nvs_commit(nvs);
                        nvs_close(nvs);
                        backend_discovery_set_url("");
                        ESP_LOGI(TAG, "Cleared backend URL from NVS");
                    }
                }
            }
            cJSON_Delete(root);
        }
    }

    const char *resp = "{\"status\":\"ok\"}";
    *outbuf = (uint8_t *)strdup(resp);
    if (*outbuf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(resp) + 1;
    return ESP_OK;
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected! IP: %s", ip_str);
        display_status_set("WiFi Connected", ip_str);
    }
}

esp_err_t wifi_onboard_ble_start(void)
{
    /* Ensure existing STA attempts and reconnect timers are halted */
    wifi_manager_stop();

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char service_name[24];
    snprintf(service_name, sizeof(service_name), "%s%02X%02X", MIMI_BLE_PROV_PREFIX, mac[4], mac[5]);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Starting BLE Wi-Fi Provisioning");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "BLE device name: %s", service_name);
    ESP_LOGI(TAG, "BLE provisioning started");
    ESP_LOGI(TAG, "Open 'ESP BLE Provisioning' app on phone or Chrome Web Bluetooth");
    ESP_LOGI(TAG, "Connect to '%s' to configure Wi-Fi", service_name);

    display_status_set("WiFi Setup (BLE)", service_name);

    esp_event_handler_instance_t ip_handler_instance = NULL;
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL, &ip_handler_instance);

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        .app_event_handler = {
            .event_cb = prov_event_handler,
            .user_data = NULL,
        },
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    /* Create custom endpoint for custom configuration (e.g. backend URL) */
    wifi_prov_mgr_endpoint_create("custom-config");

    // ponytail: Security 0 used for personal companion; upgrade to Security 1 (PoP) if deployed in untrusted public environments.
    wifi_prov_security_t security = WIFI_PROV_SECURITY_0;
    const char *pop = NULL;
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, NULL));

    /* Register handler for custom endpoint */
    wifi_prov_mgr_endpoint_register("custom-config", custom_config_prov_handler, NULL);

    /* Block until provisioning completes */
    wifi_prov_mgr_wait();

    if (ip_handler_instance) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler_instance);
    }

    wifi_prov_mgr_deinit();

    /* Cleanly disconnect and stop Wi-Fi so the AP receives an 802.11 Deauth/Disassoc frame
     * and clears the station from its active client table before ESP reboots. */
    ESP_LOGI(TAG, "Provisioning complete. Disconnecting Wi-Fi cleanly before restart...");
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Restarting in %dms...", MIMI_BLE_PROV_RESTART_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(MIMI_BLE_PROV_RESTART_DELAY_MS));
    esp_restart();

    return ESP_OK;
}
