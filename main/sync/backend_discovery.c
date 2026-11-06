#include "backend_discovery.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

#include "mimi_config.h"
#include "display/display_status.h"

#define BACKEND_MDNS_SERVICE "_espet"
#define BACKEND_MDNS_PROTO   "_tcp"
#define LOOKUP_TIMEOUT_MS    3000
#define RETRY_DELAY_MS       5000
#define MAX_ATTEMPTS         6

static const char *TAG = "discovery";

static char s_base_url[96];
static volatile bool s_task_running;

static void store_base_url(const char *url)
{
    strlcpy(s_base_url, url, sizeof(s_base_url));
}

void backend_discovery_base_url(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    /* s_base_url is only written by the single discovery task; readers either
     * see the full previous or full next URL, never a torn one. */
    if (s_base_url[0] != '\0') {
        strlcpy(out, s_base_url, out_sz);
    } else {
        strlcpy(out, MIMI_BACKEND_URL, out_sz);
    }
}

static bool resolve_once(char *out, size_t out_sz)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(BACKEND_MDNS_SERVICE, BACKEND_MDNS_PROTO,
                                   LOOKUP_TIMEOUT_MS, 1, &results);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS query failed: %s", esp_err_to_name(err));
        return false;
    }

    int count = 0;
    bool found = false;
    for (mdns_result_t *r = results; r && !found; r = r->next, count++) {
        ESP_LOGI(TAG, "mDNS result: %s port=%u", r->instance_name ? r->instance_name : "?",
                 (unsigned)r->port);

        for (mdns_ip_addr_t *a = r->addr; a && !found; a = a->next) {
            if (a->addr.type != ESP_IPADDR_TYPE_V4) continue;
            char ip[16];
            esp_ip4addr_ntoa(&a->addr.u_addr.ip4, ip, sizeof(ip));
            snprintf(out, out_sz, "http://%s:%u", ip, (unsigned)r->port);
            found = true;
        }

        /* If no IP in PTR record, resolve hostname or instance name via mdns_query_a */
        const char *candidates[2] = { r->hostname, r->instance_name };
        for (int c = 0; c < 2 && !found; c++) {
            const char *h = candidates[c];
            if (!h || !h[0]) continue;
            char clean_host[64];
            strlcpy(clean_host, h, sizeof(clean_host));
            char *dot = strstr(clean_host, ".local");
            if (dot) *dot = '\0';
            esp_ip4_addr_t addr;
            if (mdns_query_a(clean_host, LOOKUP_TIMEOUT_MS, &addr) == ESP_OK) {
                char ip[16];
                esp_ip4addr_ntoa(&addr, ip, sizeof(ip));
                snprintf(out, out_sz, "http://%s:%u", ip, (unsigned)r->port);
                found = true;
            }
        }
    }
    mdns_query_results_free(results);
    if (!found) ESP_LOGW(TAG, "mDNS query ok but %d result(s), none usable", count);
    return found;
}

static void discovery_task(void *arg)
{
    char url[96];
    /* Let boot-time allocations settle before creating the mDNS task */
    vTaskDelay(pdMS_TO_TICKS(3000));
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        if (resolve_once(url, sizeof(url))) {
            store_base_url(url);
            ESP_LOGI(TAG, "Backend discovered via mDNS: %s", url);
            break;
        }
        ESP_LOGW(TAG, "Backend not found via mDNS (attempt %d/%d)", attempt, MAX_ATTEMPTS);
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
    }
    if (s_base_url[0] == '\0') {
        ESP_LOGI(TAG, "Backend discovery using compiled-in fallback: %s", MIMI_BACKEND_URL);
    }
    s_task_running = false;
    vTaskDelete(NULL);
}

void backend_discovery_start(void)
{
    if (s_task_running) return;
    static bool mdns_ready;
    if (!mdns_ready) {
        esp_err_t err = mdns_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
            return;
        }
        mdns_ready = true;
    }
    s_task_running = true;
    if (xTaskCreatePinnedToCore(discovery_task, "backend_disc", 4096, NULL,
                                tskIDLE_PRIORITY + 2, NULL, 0) != pdPASS) {
        s_task_running = false;
        ESP_LOGW(TAG, "Failed to spawn discovery task");
    }
}

void backend_discovery_invalidate(void)
{
    if (s_base_url[0] == '\0' && s_task_running) {
        return;   /* nothing cached and a lookup is already in flight */
    }
    if (s_base_url[0] != '\0') {
        ESP_LOGW(TAG, "cached backend URL dropped — re-discovering");
    }
    s_base_url[0] = '\0';   /* readers now fall back to MIMI_BACKEND_URL */
    backend_discovery_start();   /* no-op if a pass is still running */
}
