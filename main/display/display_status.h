/*
 * display_status — Status and expressive face controller for ESPET display.
 */
#pragma once

#include <stddef.h>
#include "display_face.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_LINE_MAX 32

/* Call once from app_main before any display_status_set(). */
void display_status_init(void);

/* Set status text or trigger corresponding face expression. Thread-safe. */
void display_status_set(const char *line1, const char *line2);

/* Display text and hold for at least min_hold_ms before idle face is allowed. */
void display_status_set_text_timed(const char *line1, const char *line2, uint32_t min_hold_ms);

/* Extend current text hold duration (e.g. after speech ends). */
void display_status_extend_hold(uint32_t extra_ms);

/* Check if text hold is currently active. */
bool display_status_is_held(void);

/* Standard idle screen (resumes full-screen living eyes). */
void display_status_idle(void);

#ifdef __cplusplus
}
#endif
