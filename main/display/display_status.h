/*
 * display_status — single-line text status for the device display.
 *
 * Placeholder implementation: every status change is logged to the serial
 * console. When the new display hardware is chosen, implement
 * display_status_output() below with the real driver — all call sites
 * (agent loop, tools, voice, wifi, discovery) already feed this module.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_LINE_MAX 32

/* Call once from app_main before any display_status_set(). */
void display_status_init(void);

/* Set the two-line display text. Thread-safe, last write wins. */
void display_status_set(const char *line1, const char *line2);

/* Standard idle screen. */
void display_status_idle(void);

#ifdef __cplusplus
}
#endif
