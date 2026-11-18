#include "tools/button_handler.h"
#include "tools/tool_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "button_handler";

static button_press_down_cb_t s_down_cb = NULL;
static button_press_up_cb_t s_up_cb = NULL;
static button_long_press_cb_t s_long_press_cb = NULL;
static QueueHandle_t s_btn_evt_queue = NULL;

typedef struct {
    int pin;
    bool level;
} btn_evt_t;

static int64_t s_last_snap_time = 0;
/* Presses before this timestamp are coupling glitches: refreshed whenever
 * the speaker is busy, expiring AUDIO_GLITCH_COOLDOWN_MS after playback. */
static int64_t s_audio_busy_until = 0;
#define DEBOUNCE_MS 30
/* Noise filtering for the camera button (GPIO 41 has only the weak internal
 * pull-up and sits next to the I2S pins — the wake-ack speaker burst was
 * coupling in as phantom presses, snapping photos whenever the user said
 * "Hi ESP"). A real click holds LOW for at least MIN_PRESS_MS, a new click
 * is ignored within CLICK_LOCKOUT_MS of the previous one, and presses seen
 * while the speaker is playing (plus a cool-down, since the coupled pulse
 * can outlast playback by a few ms) are treated as coupling glitches. */
#define MIN_PRESS_MS    80
#define CLICK_LOCKOUT_MS 400
#define AUDIO_GLITCH_COOLDOWN_MS 200

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
    
    // State variables for the camera button (multimodal push-to-talk: press down=snap+record, release=send)
    int64_t press_time = 0;
    bool is_pressed = false;
    bool long_press_handled = false;

    while (1) {
        /* Refresh the speaker-glitch window every tick so presses arriving
         * between queue events are still gated while the speaker plays. */
        int64_t tick_now = esp_timer_get_time() / 1000;
        if (tool_audio_is_speaker_busy()) {
            s_audio_busy_until = tick_now + AUDIO_GLITCH_COOLDOWN_MS;
        }

        bool got_evt = xQueueReceive(s_btn_evt_queue, &evt, pdMS_TO_TICKS(50));
        int64_t now = esp_timer_get_time() / 1000;

#if (GPIO_BTN_WHITE_SNAP >= 0)
        /* Periodic check: if button is currently pressed and held for >= 5000ms */
        if (is_pressed && !long_press_handled) {
            if (gpio_get_level((gpio_num_t)GPIO_BTN_WHITE_SNAP) == 0) {
                if (now - press_time >= 5000) {
                    long_press_handled = true;
                    ESP_LOGW(TAG, "Camera Button (GPIO %d): 5-second long press detected!", GPIO_BTN_WHITE_SNAP);
                    if (s_long_press_cb) {
                        s_long_press_cb();
                    }
                }
            } else {
                /* Hardware pin is no longer low (missed edge or bounce) */
                is_pressed = false;
            }
        }

        if (got_evt && evt.pin == GPIO_BTN_WHITE_SNAP) {
            bool current_level_pressed = !evt.level; // LOW means pressed

            if (current_level_pressed && !is_pressed) {
                if (tool_audio_is_speaker_busy()) {
                    /* Speaker is driving the I2S bus right now — a press
                     * edge seen here is coupling from the amp, not a click. */
                    ESP_LOGW(TAG, "Camera Button (GPIO %d): press dropped — speaker active", GPIO_BTN_WHITE_SNAP);
                    xQueueReset(s_btn_evt_queue);
                    continue;
                }
                if (now < s_audio_busy_until) {
                    /* Cool-down right after playback: the coupled pulse
                     * can outlast the speaker burst by a few ms. */
                    ESP_LOGW(TAG, "Camera Button (GPIO %d): press dropped — post-playback glitch window", GPIO_BTN_WHITE_SNAP);
                    xQueueReset(s_btn_evt_queue);
                    continue;
                }
                if (gpio_get_level((gpio_num_t)GPIO_BTN_WHITE_SNAP) != 0) {
                    /* ISR saw LOW but the line has already recovered by
                     * the time this task ran — transient, not a press. */
                    ESP_LOGW(TAG, "Camera Button (GPIO %d): press dropped — pin already released (noise)", GPIO_BTN_WHITE_SNAP);
                    xQueueReset(s_btn_evt_queue);
                    continue;
                }
                if (now - s_last_snap_time < CLICK_LOCKOUT_MS) {
                    /* Still inside the lockout window after a handled click */
                    xQueueReset(s_btn_evt_queue);
                    continue;
                }

                // Button pressed down!
                is_pressed = true;
                long_press_handled = false;
                press_time = now;
                xQueueReset(s_btn_evt_queue);
                ESP_LOGI(TAG, "Camera Button (GPIO %d): Press Down", GPIO_BTN_WHITE_SNAP);

                if (s_down_cb) {
                    s_down_cb();
                }
            } else if (!current_level_pressed && is_pressed) {
                // Button released!
                is_pressed = false;
                uint32_t hold_duration_ms = (uint32_t)(now - press_time);
                s_last_snap_time = now;
                xQueueReset(s_btn_evt_queue);

                if (long_press_handled) {
                    ESP_LOGI(TAG, "Camera Button (GPIO %d): Released after 5s long press — ignoring release",
                             GPIO_BTN_WHITE_SNAP);
                    long_press_handled = false;
                } else if (hold_duration_ms < MIN_PRESS_MS) {
                    ESP_LOGW(TAG, "Camera Button (GPIO %d): %u ms pulse — below %d ms, treated as noise",
                             GPIO_BTN_WHITE_SNAP, (unsigned)hold_duration_ms, MIN_PRESS_MS);
                    if (s_up_cb) {
                        s_up_cb(0); /* notify cancel so frame buffer is freed */
                    }
                } else {
                    ESP_LOGI(TAG, "Camera Button (GPIO %d): Press Released (hold %u ms)",
                             GPIO_BTN_WHITE_SNAP, (unsigned)hold_duration_ms);
                    if (s_up_cb) {
                        s_up_cb(hold_duration_ms);
                    }
                }
            }
        }
#endif
    }
}

esp_err_t button_handler_init(button_press_down_cb_t down_cb,
                              button_press_up_cb_t up_cb,
                              button_long_press_cb_t long_press_cb)
{
    s_down_cb = down_cb;
    s_up_cb = up_cb;
    s_long_press_cb = long_press_cb;

    s_btn_evt_queue = xQueueCreate(10, sizeof(btn_evt_t));
    if (!s_btn_evt_queue) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_FAIL;
    }

    uint64_t pin_mask = 0;
#if (GPIO_BTN_WHITE_SNAP >= 0)
    pin_mask |= (1ULL << GPIO_BTN_WHITE_SNAP);
#endif

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
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
#if (GPIO_BTN_WHITE_SNAP >= 0)
    gpio_isr_handler_add(GPIO_BTN_WHITE_SNAP, gpio_isr_handler, (void *)GPIO_BTN_WHITE_SNAP);
#endif

    ESP_LOGI(TAG, "Button handler initialized (Cam Button=%d: short=snap photo, hold=video)",
             GPIO_BTN_WHITE_SNAP);
    return ESP_OK;
}

bool button_is_white_pressed(void)
{
#if (GPIO_BTN_WHITE_SNAP >= 0)
    return gpio_get_level(GPIO_BTN_WHITE_SNAP) == 0;
#else
    return false;
#endif
}
