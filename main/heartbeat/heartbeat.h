#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize the heartbeat service (logs ready state).
 */
esp_err_t heartbeat_init(void);

/**
 * Start periodic heartbeat checks (dispatched via cron task).
 * Checks HEARTBEAT.md periodically and sends a prompt to the agent if actionable tasks are found.
 */
esp_err_t heartbeat_start(void);

/**
 * Stop periodic heartbeat checks.
 */
void heartbeat_stop(void);

/**
 * Check if heartbeat polling is enabled.
 */
bool heartbeat_is_enabled(void);

/**
 * Manually trigger a heartbeat check (for CLI testing).
 * Returns true if the agent was prompted, false if no tasks found.
 */
bool heartbeat_trigger(void);
