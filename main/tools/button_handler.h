#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

#define GPIO_BTN_WHITE_SNAP  38  /* White Button: Camera Snap / Capture Image */
#define GPIO_BTN_GREEN_PTT   39  /* Green Button: Push-To-Talk Mic Recording */

typedef void (*button_short_press_cb_t)(void);
typedef void (*button_long_press_cb_t)(bool is_starting);
typedef void (*button_ptt_cb_t)(bool is_pressed);

/**
 * Initialize hardware button interrupt handlers for GPIO 38 & 39.
 */
esp_err_t button_handler_init(button_short_press_cb_t short_cb, button_long_press_cb_t long_cb, button_ptt_cb_t ptt_cb);

/**
 * Check current hardware state of push buttons.
 * Returns true if button is pressed (LOW).
 */
bool button_is_white_pressed(void);
bool button_is_green_pressed(void);
