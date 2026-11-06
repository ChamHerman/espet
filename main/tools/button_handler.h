#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "mimi_config.h"
#include <stdbool.h>

#define GPIO_BTN_WHITE_SNAP  MIMI_GPIO_BTN_CAM   /* Camera Button (GPIO 41): short=snap to Telegram, hold=video */

typedef void (*button_short_press_cb_t)(void);
typedef void (*button_long_press_cb_t)(bool is_starting);

/**
 * Initialize hardware button interrupt handlers for the camera button.
 */
esp_err_t button_handler_init(button_short_press_cb_t short_cb, button_long_press_cb_t long_cb);

/**
 * Check current hardware state of push buttons.
 * Returns true if button is pressed (LOW).
 */
bool button_is_white_pressed(void);

