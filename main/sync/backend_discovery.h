/*
 * backend_discovery — locate the companion FastAPI backend at runtime.
 *
 * The backend advertises itself over mDNS/Bonjour as "_espet._tcp.local."
 * (see espet-backend main.py). This module resolves it after Wi-Fi connects
 * and caches the base URL "http://<ip>:<port>", so a changing laptop IP no
 * longer requires a rebuild. Until a lookup succeeds — or if it never does —
 * callers transparently fall back to the compile-time MIMI_BACKEND_URL.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Kick off background mDNS discovery of the backend.
 *
 * Spawns a low-priority task that queries "_espet._tcp.local." and refreshes
 * the cached base URL. Safe to call multiple times; also re-usable as a
 * re-resolve trigger after backend connection failures.
 */
void backend_discovery_start(void);

/**
 * @brief Initialize discovery caches from NVS safely on DRAM stack (app_main).
 *
 * Primes in-memory caches so subsequent calls to backend_discovery_base_url()
 * never access SPI flash / NVS, allowing callers running with stacks in PSRAM
 * (like camera_stream_task or agent_loop_task) to safely retrieve the backend URL.
 */
void backend_discovery_init(void);

/**
 * @brief Return true if @p url points to a loopback address (localhost, 127.x, ::1, 0.0.0.0).
 *
 * On ESP32, loopback refers to the microcontroller itself and can never reach
 * the host PC running espet-backend.
 */
bool backend_discovery_is_loopback_url(const char *url);

/**
 * @brief Update in-memory backend URL immediately (called e.g. after BLE provisioning).
 */
void backend_discovery_set_url(const char *url);

/**
 * @brief Copy the current backend base URL (with no trailing slash) into @p out.
 *
 * Returns the discovered URL when available, otherwise the compile-time
 * MIMI_BACKEND_URL fallback.
 */
void backend_discovery_base_url(char *out, size_t out_sz);

/**
 * @brief Drop the cached URL and re-run discovery in the background.
 *
 * Call from failure paths (STT/TTS connection errors) so a backend that
 * moved IP or came up after boot is found again without a reboot. No-op
 * while a discovery pass is already running.
 */
void backend_discovery_invalidate(void);

#ifdef __cplusplus
}
#endif
