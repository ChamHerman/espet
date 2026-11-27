#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Safe user-accessible GPIO pins (41 = Green Button, 42 = White LED).
 * Excludes I2S audio (21,38,39,40), OLED I2C (1,2), Camera DVP (4-18),
 * USB JTAG (19,20), Flash/PSRAM (26-37), and UART0 (43,44). */
#define MIMI_GPIO_MIN_PIN 41
#define MIMI_GPIO_MAX_PIN 42
#define MIMI_GPIO_ALLOWED_CSV "41,42"

/**
 * Check if a pin is allowed for user GPIO operations.
 * Validates against the allowlist or default range, and blocks
 * pins reserved for flash/PSRAM on ESP32.
 */
bool gpio_policy_pin_is_allowed(int pin);

/**
 * Write a human-readable hint if the pin is forbidden for a known reason.
 * Returns true if a hint was written (and the caller should return the error).
 */
bool gpio_policy_pin_forbidden_hint(int pin, char *result, size_t result_len);
