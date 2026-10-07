#include "sync/config_sync.h"
#include "sync/backend_discovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include "mimi_config.h"
#include "wifi/wifi_manager.h"
#include "cron/cron_service.h"

static const char *TAG = "cfg_sync";

/* Response buffer size — must hold the whole /api/config bundle
   (two markdown files + memory + up to 16 job objects). */
#define SYNC_BUF_SIZE (32 * 1024)

/* Buffer for the /api/skills/sync bundle: up to 16 skills x 8 KB content
   plus JSON overhead. PSRAM-first, rejected on overflow. */
#define SKILLS_BUF_SIZE (192 * 1024)

/* Section indexes */
enum { SEC_USER = 0, SEC_SOUL, SEC_MEMORY, SEC_JOBS, SEC_DEVICE, SEC_SKILLS, SEC_COUNT };

static const char *SEC_NAMES[SEC_COUNT] = { "user", "soul", "memory", "jobs", "device", "skills" };

/* Last-applied versions, persisted in MIMI_SYNC_STATE_FILE */
static int64_t s_applied_version[SEC_COUNT];

/* Job ids received from the backend (comma-separated inside sync state).
   These are the only jobs a sync may replace or remove — jobs the agent
   created on-device never appear here and are therefore preserved. */
static char s_synced_ids[512];

/* Skill filenames received from the backend (comma-separated). Same
   ownership contract as jobs: skills flashed at build time or written by
   the agent (write_file) never appear here and are never touched by sync. */
static char s_synced_skills[1280];

static TaskHandle_t s_task = NULL;
static volatile uint32_t s_interval_s = MIMI_CONFIG_SYNC_INTERVAL_S;

/* ── SPIFFS helpers ──────────────────────────────────────────── */

static esp_err_t write_text_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for writing", path);
        return ESP_FAIL;
    }
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    if (written != len) {
        ESP_LOGE(TAG, "Short write on %s: %d/%u", path, (int)written, (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static char *read_text_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 32 * 1024) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* ── Sync state persistence ──────────────────────────────────── */

static esp_err_t save_state(void)
{
    cJSON *root = cJSON_CreateObject();
    for (int i = 0; i < SEC_COUNT; i++) {
        cJSON_AddNumberToObject(root, SEC_NAMES[i], (double)s_applied_version[i]);
    }
    cJSON_AddStringToObject(root, "synced_ids", s_synced_ids);
    cJSON_AddStringToObject(root, "synced_skills", s_synced_skills);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;

    esp_err_t err = write_text_file(MIMI_SYNC_STATE_FILE, json);
    cJSON_free(json);
    return err;
}

static void load_state(void)
{
    memset(s_applied_version, 0, sizeof(s_applied_version));
    s_synced_ids[0] = '\0';

    char *buf = read_text_file(MIMI_SYNC_STATE_FILE);
    if (!buf) return;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;

    for (int i = 0; i < SEC_COUNT; i++) {
        cJSON *v = cJSON_GetObjectItem(root, SEC_NAMES[i]);
        if (v && cJSON_IsNumber(v)) {
            s_applied_version[i] = (int64_t)v->valuedouble;
        }
    }
    const char *ids = cJSON_GetStringValue(cJSON_GetObjectItem(root, "synced_ids"));
    if (ids) {
        strncpy(s_synced_ids, ids, sizeof(s_synced_ids) - 1);
    }
    const char *skill_fns = cJSON_GetStringValue(cJSON_GetObjectItem(root, "synced_skills"));
    if (skill_fns) {
        strncpy(s_synced_skills, skill_fns, sizeof(s_synced_skills) - 1);
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded sync state: user=%lld soul=%lld mem=%lld jobs=%lld dev=%lld skills=%lld",
             (long long)s_applied_version[SEC_USER],
             (long long)s_applied_version[SEC_SOUL],
             (long long)s_applied_version[SEC_MEMORY],
             (long long)s_applied_version[SEC_JOBS],
             (long long)s_applied_version[SEC_DEVICE],
             (long long)s_applied_version[SEC_SKILLS]);
}

/* ── Cron merge ──────────────────────────────────────────────── */

static bool id_in_incoming(cJSON *incoming_ids, const char *id)
{
    cJSON *item;
    cJSON_ArrayForEach(item, incoming_ids) {
        const char *s = cJSON_GetStringValue(item);
        if (s && strcmp(s, id) == 0) return true;
    }
    return false;
}

static bool job_schedule_changed(cJSON *a, cJSON *b)
{
    /* Same id, but timing fields differ -> recompute next_run */
    const char *kind_a = cJSON_GetStringValue(cJSON_GetObjectItem(a, "kind"));
    const char *kind_b = cJSON_GetStringValue(cJSON_GetObjectItem(b, "kind"));
    if (!kind_a || !kind_b || strcmp(kind_a, kind_b) != 0) return true;

    cJSON *ia = cJSON_GetObjectItem(a, "interval_s");
    cJSON *ib = cJSON_GetObjectItem(b, "interval_s");
    if (ia || ib) {
        if (!ia || !ib || ia->valuedouble != ib->valuedouble) return true;
    }
    cJSON *ea = cJSON_GetObjectItem(a, "at_epoch");
    cJSON *eb = cJSON_GetObjectItem(b, "at_epoch");
    if (ea || eb) {
        if (!ea || !eb || ea->valuedouble != eb->valuedouble) return true;
    }
    return false;
}

/**
 * Merge the backend's job list into MIMI_CRON_FILE:
 *   - local jobs whose id is in the synced set but absent from the backend
 *     are deleted (web job removed from the dashboard)
 *   - backend jobs are upserted by id (content edited -> next_run recomputed)
 *   - local jobs NOT in the synced set (agent-created) are kept untouched
 * Then reloads the cron service from file.
 */
static esp_err_t apply_jobs(cJSON *jobs_arr)
{
    /* Load current local jobs (may include agent-created ones) */
    cJSON *local_root = NULL;
    cJSON *local_jobs = NULL;
    char *local_buf = read_text_file(MIMI_CRON_FILE);
    if (local_buf) {
        local_root = cJSON_Parse(local_buf);
        free(local_buf);
    }
    if (local_root) {
        local_jobs = cJSON_DetachItemViaPointer(local_root,
                                                cJSON_GetObjectItem(local_root, "jobs"));
        cJSON_Delete(local_root);
    }
    if (!local_jobs || !cJSON_IsArray(local_jobs)) {
        if (local_jobs) cJSON_Delete(local_jobs);
        local_jobs = cJSON_CreateArray();
    }

    /* Collect incoming ids */
    cJSON *incoming_ids = cJSON_CreateArray();
    cJSON *item;
    cJSON_ArrayForEach(item, jobs_arr) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "id"));
        if (id) cJSON_AddItemToArray(incoming_ids, cJSON_CreateString(id));
    }

    /* 1) Drop local jobs previously synced from web (they are being replaced).
       Manual iteration — items are detached from local_jobs as we walk. */
    cJSON *merged = cJSON_CreateArray();
    cJSON *tmp_item = local_jobs->child;
    while (tmp_item) {
        cJSON *next = tmp_item->next;
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(tmp_item, "id"));
        bool was_web_managed = false;
        const char *p = s_synced_ids;
        while (p && *p) {
            const char *comma = strchr(p, ',');
            size_t len = comma ? (size_t)(comma - p) : strlen(p);
            if (id && strlen(id) == len && strncmp(id, p, len) == 0) {
                was_web_managed = true;
                break;
            }
            p = comma ? comma + 1 : NULL;
        }
        /* Keep only agent-created jobs, and only if the web list has no
           conflicting id (paranoia against id collision). */
        if (!was_web_managed && !(id && id_in_incoming(incoming_ids, id))) {
            cJSON_AddItemToArray(merged, cJSON_DetachItemViaPointer(local_jobs, tmp_item));
        }
        tmp_item = next;
    }
    cJSON_Delete(local_jobs);

    /* 2) Upsert incoming jobs */
    time_t now = time(NULL);
    int count = cJSON_GetArraySize(merged);

    cJSON_ArrayForEach(item, jobs_arr) {
        cJSON *job = cJSON_Duplicate(item, true);
        if (!job) continue;

        /* Normalize to the device cron.json schema */
        cJSON_DeleteItemFromObject(job, "created_at");
        cJSON *lr = cJSON_GetObjectItem(job, "last_run");
        if (!lr) cJSON_AddNumberToObject(job, "last_run", 0);
        cJSON *nr = cJSON_GetObjectItem(job, "next_run");
        if (!nr) cJSON_AddNumberToObject(job, "next_run", 0);

        const char *kind = cJSON_GetStringValue(cJSON_GetObjectItem(job, "kind"));
        bool enabled = cJSON_IsTrue(cJSON_GetObjectItem(job, "enabled"));

        /* Recompute next_run for genuinely new/changed schedules */
        cJSON *existing = NULL;
        cJSON *m;
        cJSON_ArrayForEach(m, merged) {
            const char *a = cJSON_GetStringValue(cJSON_GetObjectItem(m, "id"));
            const char *b = cJSON_GetStringValue(cJSON_GetObjectItem(job, "id"));
            if (a && b && strcmp(a, b) == 0) { existing = m; break; }
        }

        if (existing) {
            if (job_schedule_changed(existing, job) && enabled) {
                if (kind && strcmp(kind, "every") == 0) {
                    cJSON *iv = cJSON_GetObjectItem(job, "interval_s");
                    cJSON_ReplaceItemInObject(job, "next_run",
                        cJSON_CreateNumber((double)(now + (iv ? (time_t)iv->valuedouble : 60))));
                } else {
                    cJSON *at = cJSON_GetObjectItem(job, "at_epoch");
                    cJSON_ReplaceItemInObject(job, "next_run",
                        cJSON_CreateNumber(at ? at->valuedouble : 0));
                }
            }
            cJSON_ReplaceItemViaPointer(merged, existing, job);
        } else if (count < MIMI_CRON_MAX_JOBS) {
            if (enabled) {
                if (kind && strcmp(kind, "every") == 0) {
                    cJSON *iv = cJSON_GetObjectItem(job, "interval_s");
                    cJSON_ReplaceItemInObject(job, "next_run",
                        cJSON_CreateNumber((double)(now + (iv ? (time_t)iv->valuedouble : 60))));
                } else {
                    cJSON *at = cJSON_GetObjectItem(job, "at_epoch");
                    if (at && at->valuedouble > (double)now) {
                        cJSON_ReplaceItemInObject(job, "next_run",
                            cJSON_CreateNumber(at->valuedouble));
                    }
                }
            }
            cJSON_AddItemToArray(merged, job);
            count++;
        } else {
            cJSON_Delete(job);  /* device job table full */
        }
    }

    /* 3) Persist merged list + remember which ids are web-managed */
    cJSON *out = cJSON_CreateObject();
    cJSON_AddItemToObject(out, "jobs", merged);
    char *json = cJSON_Print(out);
    cJSON_Delete(out);
    if (!json) {
        cJSON_Delete(incoming_ids);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = write_text_file(MIMI_CRON_FILE, json);
    cJSON_free(json);

    if (err == ESP_OK) {
        /* Rebuild the comma-separated synced id list */
        size_t off = 0;
        s_synced_ids[0] = '\0';
        cJSON_ArrayForEach(item, incoming_ids) {
            const char *id = cJSON_GetStringValue(item);
            if (!id) continue;
            size_t len = strlen(id);
            if (off + len + 2 >= sizeof(s_synced_ids)) break;
            if (off) s_synced_ids[off++] = ',';
            memcpy(s_synced_ids + off, id, len);
            off += len;
        }
        s_synced_ids[off] = '\0';

        cron_reload_jobs();
        ESP_LOGI(TAG, "Cron merge done: %d jobs in file", count);
    }

    cJSON_Delete(incoming_ids);
    return err;
}

/* ── Section appliers ────────────────────────────────────────── */

/* Skill filenames from the backend are sanitized server-side, but never
   trust a path component across the wire. */
static bool skill_filename_safe(const char *fname)
{
    return fname && fname[0] && strlen(fname) < 64 &&
           !strchr(fname, '/') && !strstr(fname, "..");
}

/**
 * Apply the backend's skill list to /spiffs/skills/:
 *   - files previously synced from the web but absent from the incoming
 *     list (deleted or disabled on the dashboard) are unlinked
 *   - every incoming skill is written (overwritten) as <filename>
 *   - skills flashed at build time or created by the agent are preserved
 *     (their filenames never enter s_synced_skills)
 * Returns ESP_OK only if every incoming skill was written.
 */
static esp_err_t apply_skills(cJSON *skills_arr)
{
    char path[96];

    /* 1) Remove web-managed files that are no longer in the payload */
    const char *p = s_synced_skills;
    while (p && *p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        char fname[64];
        if (len > 0 && len < sizeof(fname)) {
            memcpy(fname, p, len);
            fname[len] = '\0';

            if (skill_filename_safe(fname)) {
                bool still_present = false;
                cJSON *item;
                cJSON_ArrayForEach(item, skills_arr) {
                    const char *fn = cJSON_GetStringValue(cJSON_GetObjectItem(item, "filename"));
                    if (fn && strcmp(fn, fname) == 0) { still_present = true; break; }
                }
                if (!still_present) {
                    snprintf(path, sizeof(path), MIMI_SKILLS_PREFIX "%s", fname);
                    if (unlink(path) == 0) {
                        ESP_LOGI(TAG, "Removed web skill %s", fname);
                    }
                }
            }
        }
        p = comma ? comma + 1 : NULL;
    }

    /* 2) Write every incoming skill */
    cJSON *item;
    cJSON_ArrayForEach(item, skills_arr) {
        const char *fname = cJSON_GetStringValue(cJSON_GetObjectItem(item, "filename"));
        const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(item, "content"));
        if (!skill_filename_safe(fname) || !content) continue;
        snprintf(path, sizeof(path), MIMI_SKILLS_PREFIX "%s", fname);
        esp_err_t err = write_text_file(path, content);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed writing skill %s", fname);
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t apply_section(int sec, cJSON *bundle)
{
    esp_err_t err = ESP_OK;

    switch (sec) {
    case SEC_USER:
        err = write_text_file(MIMI_USER_FILE,
                              cJSON_GetStringValue(cJSON_GetObjectItem(bundle, "user_md")) ?: "");
        break;
    case SEC_SOUL:
        err = write_text_file(MIMI_SOUL_FILE,
                              cJSON_GetStringValue(cJSON_GetObjectItem(bundle, "soul_md")) ?: "");
        break;
    case SEC_MEMORY:
        err = write_text_file(MIMI_MEMORY_FILE,
                              cJSON_GetStringValue(cJSON_GetObjectItem(bundle, "memory_md")) ?: "");
        break;
    case SEC_JOBS: {
        cJSON *jobs = cJSON_GetObjectItem(bundle, "jobs");
        if (jobs && cJSON_IsArray(jobs)) {
            err = apply_jobs(jobs);
        }
        break;
    }
    case SEC_DEVICE: {
        cJSON *dev = cJSON_GetObjectItem(bundle, "device");
        if (!dev) break;
        const char *posix_tz = cJSON_GetStringValue(cJSON_GetObjectItem(dev, "posix_tz"));
        if (posix_tz && posix_tz[0]) {
            setenv("TZ", posix_tz, 1);
            tzset();
            ESP_LOGI(TAG, "Timezone applied: %s (%s)",
                     cJSON_GetStringValue(cJSON_GetObjectItem(dev, "timezone")) ?: "?", posix_tz);
        }
        cJSON *interval = cJSON_GetObjectItem(dev, "sync_interval_s");
        if (interval && cJSON_IsNumber(interval) && interval->valuedouble >= 60) {
            s_interval_s = (uint32_t)interval->valuedouble;
        }
        break;
    }
    case SEC_SKILLS:
        /* Applied separately in sync_skills() — never reached via
           apply_section (sync_once skips it before the version would be
           recorded as applied without the files being written). */
        break;
    }

    if (err == ESP_OK) {
        s_applied_version[sec] = (int64_t)cJSON_GetObjectItem(
                                             cJSON_GetObjectItem(bundle, "versions"),
                                             SEC_NAMES[sec])->valuedouble;
        ESP_LOGI(TAG, "Applied section '%s' (v%lld)", SEC_NAMES[sec],
                 (long long)s_applied_version[sec]);
    }
    return err;
}

/* ── HTTP pull ───────────────────────────────────────────────── */

typedef struct {
    char *buf;
    size_t used;
    size_t cap;
    bool overflow;
} http_body_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_body_t *body = (http_body_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && body) {
        if (body->used + evt->data_len > body->cap) {
            body->overflow = true;
            return ESP_OK;
        }
        memcpy(body->buf + body->used, evt->data, evt->data_len);
        body->used += evt->data_len;
        body->buf[body->used] = '\0';
    }
    return ESP_OK;
}

static cJSON *fetch_json(const char *path, size_t buf_size)
{
    char *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(buf_size);
    if (!buf) {
        ESP_LOGE(TAG, "No memory for sync buffer (%u KB)", (unsigned)(buf_size / 1024));
        return NULL;
    }
    buf[0] = '\0';

    http_body_t body = { .buf = buf, .used = 0, .cap = buf_size - 1, .overflow = false };

    char url[160];
    char auth_hdr[96];
    char base[96];
    backend_discovery_base_url(base, sizeof(base));
    snprintf(url, sizeof(url), "%s%s", base, path);
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", MIMI_BACKEND_API_KEY);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
        .user_data = &body,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(buf);
        return NULL;
    }
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Authorization", auth_hdr);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    cJSON *result = NULL;
    if (err == ESP_OK && status == 200 && !body.overflow && body.used > 0) {
        result = cJSON_Parse(buf);
        if (!result) {
            ESP_LOGE(TAG, "Invalid JSON from %s (len=%u)", path, (unsigned)body.used);
        }
    } else if (err == ESP_OK && status != 200) {
        ESP_LOGW(TAG, "GET %s -> HTTP %d", path, status);
    } else if (body.overflow) {
        ESP_LOGE(TAG, "%s response exceeds %u KB buffer", path, (unsigned)(buf_size / 1024));
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %s failed: %s", path, esp_err_to_name(err));
    }

    free(buf);
    return result;
}

static cJSON *fetch_config_bundle(void)
{
    return fetch_json("/api/config", SYNC_BUF_SIZE);
}

static cJSON *fetch_skills_bundle(void)
{
    return fetch_json("/api/skills/sync", SKILLS_BUF_SIZE);
}

/**
 * Skills ride their own endpoint (content is far bigger than the config
 * bundle), so they are applied outside the generic apply_section loop:
 * fetch the bundle, apply it, record the version + synced filenames.
 */
static void sync_skills(int64_t incoming_version)
{
    cJSON *bundle = fetch_skills_bundle();
    if (!bundle) return;

    cJSON *skills = cJSON_GetObjectItem(bundle, "skills");
    if (skills && cJSON_IsArray(skills)) {
        if (apply_skills(skills) == ESP_OK) {
            /* Rebuild the web-managed filename list from what we applied */
            size_t off = 0;
            s_synced_skills[0] = '\0';
            cJSON *item;
            cJSON_ArrayForEach(item, skills) {
                const char *fname = cJSON_GetStringValue(cJSON_GetObjectItem(item, "filename"));
                if (!fname) continue;
                size_t len = strlen(fname);
                if (off + len + 2 >= sizeof(s_synced_skills)) break;
                if (off) s_synced_skills[off++] = ',';
                memcpy(s_synced_skills + off, fname, len);
                off += len;
            }
            s_synced_skills[off] = '\0';

            s_applied_version[SEC_SKILLS] = incoming_version;
            save_state();
            ESP_LOGI(TAG, "Applied skills (v%lld, %d files)", (long long)incoming_version,
                     cJSON_GetArraySize(skills));
        }
    } else {
        ESP_LOGW(TAG, "Skills bundle missing 'skills' array");
    }

    cJSON_Delete(bundle);
}

/* ── Sync cycle & task ───────────────────────────────────────── */

static void sync_once(void)
{
    cJSON *bundle = fetch_config_bundle();
    if (!bundle) return;

    cJSON *versions = cJSON_GetObjectItem(bundle, "versions");
    if (!versions || !cJSON_IsObject(versions)) {
        cJSON_Delete(bundle);
        return;
    }

    bool changed = false;
    for (int sec = 0; sec < SEC_COUNT; sec++) {
        /* Skills need their own fetch (bundle too big for the config buffer) */
        if (sec == SEC_SKILLS) continue;

        cJSON *v = cJSON_GetObjectItem(versions, SEC_NAMES[sec]);
        if (!v || !cJSON_IsNumber(v)) continue;
        if ((int64_t)v->valuedouble <= s_applied_version[sec]) continue;

        if (apply_section(sec, bundle) == ESP_OK) {
            changed = true;
        }
    }

    /* Skills: separate pull, only when the version actually advanced */
    cJSON *sv = cJSON_GetObjectItem(versions, "skills");
    if (sv && cJSON_IsNumber(sv) &&
        (int64_t)sv->valuedouble > s_applied_version[SEC_SKILLS]) {
        sync_skills((int64_t)sv->valuedouble);
    }

    if (changed) {
        save_state();
    }

    cJSON_Delete(bundle);
}

static void sync_task_main(void *arg)
{
    (void)arg;

    /* Wait for network — config sync is useless without the backend */
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    /* Small settle delay so the backend is reachable after association */
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "Config sync started (every %us)", (unsigned)s_interval_s);

    for (;;) {
        /* Re-run mDNS discovery each cycle so a changed backend IP self-heals */
        backend_discovery_start();
        sync_once();
        /* Sleep in 1s slices so a new interval from the web takes effect */
        uint32_t remaining = s_interval_s;
        while (remaining > 0 && s_task) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            remaining--;
        }
    }
}

/* ── Public API ──────────────────────────────────────────────── */

esp_err_t config_sync_init(void)
{
    load_state();
    return ESP_OK;
}

esp_err_t config_sync_start(void)
{
#if MIMI_CONFIG_SYNC_ENABLED
    if (s_task) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(
        sync_task_main,
        "cfg_sync",
        MIMI_CONFIG_SYNC_STACK,
        NULL,
        MIMI_CONFIG_SYNC_PRIO,
        &s_task,
        MIMI_CONFIG_SYNC_CORE
    );
    if (ok != pdPASS || !s_task) {
        s_task = NULL;
        ESP_LOGE(TAG, "Failed to create config sync task");
        return ESP_FAIL;
    }
#else
    ESP_LOGI(TAG, "Config sync disabled (MIMI_CONFIG_SYNC_ENABLED=0)");
#endif
    return ESP_OK;
}
