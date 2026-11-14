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

/* Standard idle screen (resumes full-screen living eyes). */
void display_status_idle(void);

#ifdef __cplusplus
}
#endif
