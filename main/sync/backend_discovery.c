#include "backend_discovery.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

#include "mimi_config.h"
#include "display/display_status.h"
#include "nvs_flash.h"
#include "nvs.h"

#define BACKEND_MDNS_SERVICE "_espet"
#define BACKEND_MDNS_PROTO   "_tcp"
#define LOOKUP_TIMEOUT_MS    3000
#define RETRY_DELAY_MS       5000
#define MAX_ATTEMPTS         6
#define MAX_MDNS_RESULTS     8

static const char *TAG = "discovery";

static char s_base_url[96];
static char s_nvs_url[96];
static volatile bool s_task_running;

static void store_base_url(const char *url)
{
    strlcpy(s_base_url, url, sizeof(s_base_url));
}

static bool get_nvs_backend_url(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return false;
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_BACKEND, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = out_sz;
        esp_err_t err = nvs_get_str(nvs, MIMI_NVS_KEY_BACKEND_URL, out, &len);
        nvs_close(nvs);
        if (err == ESP_OK && out[0] != '\0') {
            return true;
        }
    }
    return false;
}

void backend_discovery_init(void)
{
    /* Safe to call from app_main (main task stack in DRAM).
     * Reads NVS once and primes in-memory caches so tasks running with
     * stacks in PSRAM never invoke SPI flash / NVS. */
    if (get_nvs_backend_url(s_nvs_url, sizeof(s_nvs_url))) {
        store_base_url(s_nvs_url);
        ESP_LOGI(TAG, "Backend discovery primed from NVS: %s", s_nvs_url);
    }
}

void backend_discovery_set_url(const char *url)
{
    if (url && url[0] != '\0') {
        strlcpy(s_nvs_url, url, sizeof(s_nvs_url));
        store_base_url(url);
    } else {
        s_nvs_url[0] = '\0';
        s_base_url[0] = '\0';
    }
}

void backend_discovery_base_url(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    /* PURE IN-MEMORY: Never call NVS here because callers like camera_stream_task
     * or agent_loop_task have stacks in PSRAM. SPI flash access disables cache
     * which triggers esp_task_stack_is_sane_cache_disabled() panic. */
    if (s_base_url[0] != '\0') {
        strlcpy(out, s_base_url, out_sz);
        return;
    }
    if (s_nvs_url[0] != '\0') {
        strlcpy(out, s_nvs_url, out_sz);
        return;
    }
    /* Fallback: compiled-in default */
    strlcpy(out, MIMI_BACKEND_URL, out_sz);
}

static bool resolve_once(char *out, size_t out_sz)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(BACKEND_MDNS_SERVICE, BACKEND_MDNS_PROTO,
                                   LOOKUP_TIMEOUT_MS, MAX_MDNS_RESULTS, &results);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS query failed: %s", esp_err_to_name(err));
        return false;
    }

    int count = 0;
    char first_url[96] = {0};
    bool matched_preferred = false;

    for (mdns_result_t *r = results; r && !matched_preferred; r = r->next, count++) {
        ESP_LOGI(TAG, "mDNS result: %s port=%u", r->instance_name ? r->instance_name : "?",
                 (unsigned)r->port);

        char candidate_url[96] = {0};
        char candidate_ip[16] = {0};

        for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
            if (a->addr.type != ESP_IPADDR_TYPE_V4) continue;
            esp_ip4addr_ntoa(&a->addr.u_addr.ip4, candidate_ip, sizeof(candidate_ip));
            snprintf(candidate_url, sizeof(candidate_url), "http://%s:%u",
                     candidate_ip, (unsigned)r->port);
            break;
        }

        /* If no IP in PTR record, resolve hostname or instance name via mdns_query_a */
        if (candidate_url[0] == '\0') {
            const char *candidates[2] = { r->hostname, r->instance_name };
            for (int c = 0; c < 2; c++) {
                const char *h = candidates[c];
                if (!h || !h[0]) continue;
                char clean_host[64];
                strlcpy(clean_host, h, sizeof(clean_host));
                char *dot = strstr(clean_host, ".local");
                if (dot) *dot = '\0';
                esp_ip4_addr_t addr;
                if (mdns_query_a(clean_host, LOOKUP_TIMEOUT_MS, &addr) == ESP_OK) {
                    esp_ip4addr_ntoa(&addr, candidate_ip, sizeof(candidate_ip));
                    snprintf(candidate_url, sizeof(candidate_url), "http://%s:%u",
                             candidate_ip, (unsigned)r->port);
                    break;
                }
            }
        }

        if (candidate_url[0] != '\0') {
#ifdef BACKEND_IP
            if (strcmp(candidate_ip, BACKEND_IP) == 0) {
                strlcpy(out, candidate_url, out_sz);
                ESP_LOGI(TAG, "mDNS selected preferred compiled-in BACKEND_IP (%s): %s",
                         BACKEND_IP, out);
                matched_preferred = true;
                break;
            }
#endif
            if (first_url[0] == '\0') {
                strlcpy(first_url, candidate_url, sizeof(first_url));
            }
        }
    }
    mdns_query_results_free(results);

    if (matched_preferred) {
        return true;
    }
    if (first_url[0] != '\0') {
        strlcpy(out, first_url, out_sz);
        ESP_LOGI(TAG, "mDNS selected first discovered backend: %s", out);
        return true;
    }

    ESP_LOGW(TAG, "mDNS query ok but %d result(s), none usable", count);
    return false;
}

static void discovery_task(void *arg)
{
    char url[96];
    char nvs_url[96] = {0};
    bool has_nvs = get_nvs_backend_url(nvs_url, sizeof(nvs_url));

    /* If NVS was explicitly configured, prime s_base_url immediately so callers don't wait */
    if (has_nvs && s_base_url[0] == '\0') {
        store_base_url(nvs_url);
        ESP_LOGI(TAG, "Backend primed with NVS configured URL: %s", nvs_url);
    }

    /* Let boot-time allocations settle before creating the mDNS task */
    vTaskDelay(pdMS_TO_TICKS(3000));
    int max_attempts = has_nvs ? 2 : MAX_ATTEMPTS;
    for (int attempt = 1; attempt <= max_attempts; attempt++) {
        if (resolve_once(url, sizeof(url))) {
            store_base_url(url);
            ESP_LOGI(TAG, "Backend discovered via mDNS: %s", url);
            break;
        }
        ESP_LOGW(TAG, "Backend not found via mDNS (attempt %d/%d)", attempt, max_attempts);
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
    }
    if (s_base_url[0] == '\0') {
        if (has_nvs) {
            store_base_url(nvs_url);
            ESP_LOGI(TAG, "Backend discovery falling back to NVS URL: %s", nvs_url);
        } else {
            ESP_LOGI(TAG, "Backend discovery using compiled-in fallback: %s", MIMI_BACKEND_URL);
        }
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
    if (s_nvs_url[0] != '\0') {
        /* When an explicit URL is configured in NVS (e.g. cloud server),
         * keep it primed rather than dropping to empty. */
        store_base_url(s_nvs_url);
        return;
    }
    if (s_base_url[0] == '\0' && s_task_running) {
        return;   /* nothing cached and a lookup is already in flight */
    }
    if (s_base_url[0] != '\0') {
        ESP_LOGW(TAG, "cached backend URL dropped — re-discovering");
    }
    s_base_url[0] = '\0';   /* readers now fall back to MIMI_BACKEND_URL */
    backend_discovery_start();   /* no-op if a pass is still running */
}
