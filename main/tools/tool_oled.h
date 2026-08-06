#pragma once

#include "tool_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

extern mimi_tool_t oled_tool;

void oled_display_status(const char *line1, const char *line2);

/**
 * @brief Initialize the background OLED animation task
 */
void tool_oled_init_animation(void);

#ifdef __cplusplus
}
#endif
