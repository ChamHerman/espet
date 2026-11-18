#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "mimi_config.h"
#include <stdbool.h>

#define GPIO_BTN_WHITE_SNAP  MIMI_GPIO_BTN_CAM   /* Camera Button (GPIO 41): press down=snap & record, release=send */

typedef void (*button_press_down_cb_t)(void);
typedef void (*button_press_up_cb_t)(uint32_t hold_duration_ms);
typedef void (*button_long_press_cb_t)(void);

/**
 * Initialize hardware button interrupt handlers for the camera button (multimodal push-to-talk).
 * down_cb is called immediately when button is pressed down (e.g. snap picture & start mic).
 * up_cb is called when button is released with hold_duration_ms (e.g. stop mic & transcribe).
 * long_press_cb is called when button is held continuously for 5 seconds (e.g. Wi-Fi BLE provisioning reset).
 */
esp_err_t button_handler_init(button_press_down_cb_t down_cb,
                              button_press_up_cb_t up_cb,
                              button_long_press_cb_t long_press_cb);

/**
 * Check current hardware state of push buttons.
 * Returns true if button is pressed (LOW).
 */
bool button_is_white_pressed(void);

