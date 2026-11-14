#include "wifi_onboard_ble.h"
#include "sdkconfig.h"

#if CONFIG_BT_ENABLED

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

    // ponytail: Security 0 used for personal companion; upgrade to Security 1 (PoP) if deployed in untrusted public environments.
    wifi_prov_security_t security = WIFI_PROV_SECURITY_0;
    const char *pop = NULL;
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, NULL));

    /* Block until provisioning completes */
    wifi_prov_mgr_wait();

    if (ip_handler_instance) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler_instance);
    }

    wifi_prov_mgr_deinit();

    ESP_LOGI(TAG, "Provisioning complete. Restarting in %dms...", MIMI_BLE_PROV_RESTART_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(MIMI_BLE_PROV_RESTART_DELAY_MS));
    esp_restart();

    return ESP_OK;
}

#else

#include "onboard/wifi_onboard.h"
#include "display/display_status.h"
#include "esp_log.h"

static const char *TAG = "onboard_ble";

esp_err_t wifi_onboard_ble_start(void)
{
    ESP_LOGW(TAG, "BLE provisioning unavailable because CONFIG_BT_ENABLED is disabled; using SoftAP onboarding");
    display_status_set("WiFi Setup (AP)", "ESPET");
    return wifi_onboard_start(WIFI_ONBOARD_MODE_CAPTIVE);
}

#endif
