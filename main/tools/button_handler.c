#include "tools/button_handler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "button_handler";

static button_short_press_cb_t s_short_cb = NULL;
static button_long_press_cb_t s_long_cb = NULL;
static button_ptt_cb_t s_ptt_cb = NULL;
static QueueHandle_t s_btn_evt_queue = NULL;

typedef struct {
    int pin;
    bool level;
} btn_evt_t;

static int64_t s_last_snap_time = 0;
static int64_t s_last_ptt_time = 0;
#define DEBOUNCE_MS 30

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    int level = gpio_get_level((gpio_num_t)gpio_num);
    btn_evt_t evt = {
        .pin = (int)gpio_num,
        .level = (level != 0)
    };
    xQueueSendFromISR(s_btn_evt_queue, &evt, NULL);
}

static void button_task(void *arg)
{
    btn_evt_t evt;
    bool last_green_state = false;
    
    // State variables for the dual-action white button
    int64_t press_time = 0;
    bool is_pressed = false;
    bool is_long_pressing = false;
    const int64_t LONG_PRESS_THRESHOLD_MS = 3000; // 3000ms hold required

    while (1) {
        // Use a 50ms timeout so we can periodically check the hold duration
        if (xQueueReceive(s_btn_evt_queue, &evt, pdMS_TO_TICKS(50))) {
            int64_t now = esp_timer_get_time() / 1000;

            if (evt.pin == GPIO_BTN_WHITE_SNAP) {
                bool current_level_pressed = !evt.level; // LOW means pressed
                
                // If the hardware state differs from our debounced state
                if (current_level_pressed != is_pressed) {
                    // Accept the state change only if 50ms has passed since the last accepted change
                    if (now - s_last_snap_time > 50) {
                        s_last_snap_time = now;
                        is_pressed = current_level_pressed;
                        
                        if (is_pressed) {
                            // Pressed
                            is_long_pressing = false;
                            press_time = now;
                        } else {
                            // Released
                            if (is_long_pressing) {
                                ESP_LOGI(TAG, "White Button (GPIO 38): Long Press Released");
                                if (s_long_cb) {
                                    s_long_cb(false); // Stop
                                }
                            } else {
                                ESP_LOGI(TAG, "White Button (GPIO 38): Short Click Detected");
                                if (s_short_cb) {
                                    s_short_cb();
                                }
                            }
                        }
                    }
                }
            } else if (evt.pin == GPIO_BTN_GREEN_PTT) {
                bool is_green_pressed = !evt.level;
                if (is_green_pressed != last_green_state) {
                    if (now - s_last_ptt_time > 50) { // 50ms debounce
                        s_last_ptt_time = now;
                        last_green_state = is_green_pressed;
                        ESP_LOGI(TAG, "Green Button (GPIO 39): %s", is_green_pressed ? "Pressed" : "Released");
                        if (s_ptt_cb) {
                            s_ptt_cb(is_green_pressed);
                        }
                    }
                }
            }
        }

        // --- TIMEOUT CHECK for White Button ---
        if (is_pressed && !is_long_pressing) {
            int64_t now = esp_timer_get_time() / 1000;
            if ((now - press_time) >= LONG_PRESS_THRESHOLD_MS) {
                is_long_pressing = true;
                ESP_LOGI(TAG, "White Button (GPIO 38): Long Press Triggered");
                if (s_long_cb) {
                    s_long_cb(true); // Start
                }
            }
        }
    }
}

esp_err_t button_handler_init(button_short_press_cb_t short_cb, button_long_press_cb_t long_cb, button_ptt_cb_t ptt_cb)
{
    s_short_cb = short_cb;
    s_long_cb = long_cb;
    s_ptt_cb = ptt_cb;

    s_btn_evt_queue = xQueueCreate(10, sizeof(btn_evt_t));
    if (!s_btn_evt_queue) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_FAIL;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_BTN_WHITE_SNAP) | (1ULL << GPIO_BTN_GREEN_PTT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Create worker task to handle debounced button events (16KB stack for HTTPS calls) */
    xTaskCreate(button_task, "button_task", 16384, NULL, 10, NULL);

    /* Install ISR service and add handlers */
    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "gpio_install_isr_service info: %s", esp_err_to_name(isr_err));
    }
    gpio_isr_handler_add(GPIO_BTN_WHITE_SNAP, gpio_isr_handler, (void *)GPIO_BTN_WHITE_SNAP);
    gpio_isr_handler_add(GPIO_BTN_GREEN_PTT, gpio_isr_handler, (void *)GPIO_BTN_GREEN_PTT);

    ESP_LOGI(TAG, "Button handler initialized (White Snap=38, Green PTT=39)");
    return ESP_OK;
}

bool button_is_white_pressed(void)
{
    return gpio_get_level(GPIO_BTN_WHITE_SNAP) == 0;
}

bool button_is_green_pressed(void)
{
    return gpio_get_level(GPIO_BTN_GREEN_PTT) == 0;
}
