#pragma once

#include "esp_err.h"

/**
 * Configuration sync — device pulls the web dashboard's config.
 *
 * A background task fetches GET /api/config from the backend every
 * MIMI_CONFIG_SYNC_INTERVAL_S (also once shortly after boot) and applies
 * only sections whose version increased since the last sync:
 *
 *   - USER.md   -> MIMI_USER_FILE
 *   - SOUL.md   -> MIMI_SOUL_FILE
 *   - MEMORY.md -> MIMI_MEMORY_FILE
 *   - cron jobs -> merged into MIMI_CRON_FILE (web jobs upserted; jobs the
 *                  agent created on-device are preserved)
 *   - device    -> timezone via setenv("TZ") + tzset(), sync interval
 *
 * Last-applied versions live in MIMI_SYNC_STATE_FILE on SPIFFS.
 * Transport: plain HTTP + Bearer MIMI_BACKEND_API_KEY (LAN scope).
 */

/**
 * Load the last-applied version state from SPIFFS.
 * Call once during init, before start.
 */
esp_err_t config_sync_init(void);

/**
 * Create the background sync task. Safe to call before WiFi is up —
 * the task waits for a connection itself.
 */
esp_err_t config_sync_start(void);
