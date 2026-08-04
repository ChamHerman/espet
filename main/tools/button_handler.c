#include "tools/button_handler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "button_handler";

static button_snap_cb_t s_snap_cb = NULL;
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

    while (1) {
        if (xQueueReceive(s_btn_evt_queue, &evt, portMAX_DELAY)) {
            int64_t now = esp_timer_get_time() / 1000;

            if (evt.pin == GPIO_BTN_WHITE_SNAP) {
                if (!evt.level) { /* Press (LOW) */
                    if (now - s_last_snap_time > 1500) { /* 1.5s holdoff period */
                        s_last_snap_time = now;
                        ESP_LOGI(TAG, "White Button Pressed (GPIO 38): Single action triggered");
                        if (s_snap_cb) {
                            s_snap_cb();
                        }
                        /* Purge any stale bounce events queued during callback execution */
                        xQueueReset(s_btn_evt_queue);
                    }
                }
            } else if (evt.pin == GPIO_BTN_GREEN_PTT) {
                bool is_pressed = !evt.level; /* Pressed when LOW */
                if (is_pressed != last_green_state) {
                    if (now - s_last_ptt_time > 300) { /* 300ms debounce */
                        s_last_ptt_time = now;
                        last_green_state = is_pressed;
                        ESP_LOGI(TAG, "Green Button (GPIO 39): %s", is_pressed ? "Pressed" : "Released");
                        if (s_ptt_cb) {
                            s_ptt_cb(is_pressed);
                        }
                        xQueueReset(s_btn_evt_queue);
                    }
                }
            }
        }
    }
}

esp_err_t button_handler_init(button_snap_cb_t snap_cb, button_ptt_cb_t ptt_cb)
{
    s_snap_cb = snap_cb;
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
