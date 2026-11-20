#include "display_status.h"
#include "display_face.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mimi_config.h"
#include "ssd1306.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

static const char *TAG = "display";

#define DISPLAY_I2C_PORT   I2C_NUM_0
#define DISPLAY_I2C_FREQ   100000   /* 100 kHz standard mode for breadboard/cold-boot reliability */
#define DISPLAY_W          128
#define DISPLAY_H          64

static SemaphoreHandle_t s_lock;
static char s_line1[DISPLAY_LINE_MAX];
static char s_line2[DISPLAY_LINE_MAX];
static ssd1306_handle_t s_dev;
static bool s_hw_ready;

/* ── SSD1306 driver (text mode, two lines) ────────────────────
 * NOTE: the ssd1306 component asserts on I2C write failure, so
 * keep the panel connected — unplugging it at runtime will
 * trigger the component assert.
 * Pins are fixed by the authoritative pin map (.agent/ESPET_PINS.MD):
 * SCL=2, SDA=1 via MIMI_GPIO_OLED_SCL/SDA. */
static const uint8_t s_oled_addrs[2] = {0x3C, 0x3D};

static int s_found_addr = -1;
static bool s_bus_installed;
static bool s_display_dead;
static int  s_refresh_fails;

/* Recover stuck I2C bus: clock SCL up to 9 times with SDA pulled up, then STOP */
static void i2c_bus_clear(int scl, int sda)
{
    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << scl) | (1ULL << sda),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&conf);

    gpio_set_direction((gpio_num_t)scl, GPIO_MODE_OUTPUT_OD);
    for (int i = 0; i < 9; i++) {
        gpio_set_level((gpio_num_t)scl, 0);
        esp_rom_delay_us(5);
        gpio_set_level((gpio_num_t)scl, 1);
        esp_rom_delay_us(5);
        if (gpio_get_level((gpio_num_t)sda) == 1) {
            break;
        }
    }
}

/* Configure the port on the configured (scl, sda) pin pair and probe both
 * OLED addresses using standard I2C address ping. */
static bool probe_pin_pair(int scl, int sda)
{
    if (s_bus_installed) {
        i2c_driver_delete(DISPLAY_I2C_PORT);
        s_bus_installed = false;
    }

    i2c_bus_clear(scl, sda);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = DISPLAY_I2C_FREQ,
    };
    if (i2c_param_config(DISPLAY_I2C_PORT, &conf) != ESP_OK) return false;
    if (i2c_driver_install(DISPLAY_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) return false;
    s_bus_installed = true;

    for (int a = 0; a < 2; a++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (s_oled_addrs[a] << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(DISPLAY_I2C_PORT, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            s_found_addr = s_oled_addrs[a];
            return true;
        }
    }
    return false;
}

static void display_bus_teardown(void)
{
    if (s_dev) {
        ssd1306_delete(s_dev);
        s_dev = NULL;
    }
    s_found_addr = -1;
    if (s_bus_installed) {
        i2c_driver_delete(DISPLAY_I2C_PORT);
        s_bus_installed = false;
    }
}

static esp_err_t display_hw_init(void)
{
    if (s_hw_ready) return ESP_OK;
    if (s_display_dead) return ESP_FAIL;

    /* Rate limit probe attempts so we don't stall message loops if panel is truly missing */
    static int64_t s_last_probe_us = 0;
    static int s_fail_count = 0;
    int64_t now_us = esp_timer_get_time();
    if (s_last_probe_us != 0 && (now_us - s_last_probe_us) < 1000000) {
        return ESP_FAIL;
    }
    s_last_probe_us = now_us;

    ESP_LOGI(TAG, "Probing OLED on configured pins (SCL=%d, SDA=%d)...",
             MIMI_GPIO_OLED_SCL, MIMI_GPIO_OLED_SDA);

    /* Allow power rails to stabilize on cold boot */
    vTaskDelay(pdMS_TO_TICKS(100));

    bool probed = false;
    for (int attempt = 1; attempt <= 4; attempt++) {
        if (probe_pin_pair(MIMI_GPIO_OLED_SCL, MIMI_GPIO_OLED_SDA)) {
            probed = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!probed) {
        if (++s_fail_count >= 15) {
            ESP_LOGE(TAG, "OLED not responding after multiple attempts — disabled until reboot.");
            display_bus_teardown();
            s_display_dead = true;
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "OLED probe not ready on SCL=%d/SDA=%d (attempt %d/15, will retry on next message)",
                 MIMI_GPIO_OLED_SCL, MIMI_GPIO_OLED_SDA, s_fail_count);
        display_bus_teardown();
        return ESP_FAIL;
    }
    s_fail_count = 0;

    s_dev = ssd1306_create(DISPLAY_I2C_PORT, s_found_addr);
    if (s_dev == NULL) {
        ESP_LOGE(TAG, "SSD1306 create failed");
        display_bus_teardown();
        return ESP_FAIL;
    }
    /* Full controller reset: re-send the entire init sequence (charge pump,
     * max contrast 0xCF, display-on 0xAF). */
    esp_err_t init_err = ssd1306_init(s_dev);
    ssd1306_clear_screen(s_dev, 0x00);
    if (init_err != ESP_OK || ssd1306_refresh_gram(s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "panel NACKed during init (addr 0x%02X) — display disabled "
                      "until reboot", s_found_addr);
        display_bus_teardown();
        s_display_dead = true;
        return ESP_FAIL;
    }
    /* Boot self-test: blink the whole panel 3x (500 ms on / 300 ms off).
     * Any visible flash = panel electrically OK. */
    for (int i = 0; i < 3; i++) {
        ssd1306_fill_rectangle(s_dev, 0, 0, DISPLAY_W - 1, DISPLAY_H - 1, 1);
        if (ssd1306_refresh_gram(s_dev) != ESP_OK) {
            ESP_LOGE(TAG, "blink refresh %d failed", i);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        ssd1306_clear_screen(s_dev, 0x00);
        ssd1306_refresh_gram(s_dev);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGW(TAG, "self-test blink done (3x full-screen)");
    s_hw_ready = true;
    ESP_LOGI(TAG, "OLED text display ready (SCL GPIO %d, SDA GPIO %d, addr 0x%02X)",
             MIMI_GPIO_OLED_SCL, MIMI_GPIO_OLED_SDA, s_found_addr);

    /* Initialize expressive animated face engine with panel device and lock */
    display_face_init(s_dev, s_lock);
    return ESP_OK;
}

static face_state_t map_status_to_face(const char *l1, const char *l2)
{
    if (!l1 || !l1[0]) return FACE_STATE_IDLE;
    if (strcasecmp(l1, "Say Hey ESPET") == 0 || strcasecmp(l1, "idle") == 0) {
        return FACE_STATE_IDLE;
    }
    if (strcasestr(l1, "Listening") || strcasestr(l1, "Say command")) {
        return FACE_STATE_LISTEN;
    }
    if (strcasestr(l1, "Thinking") || strcasestr(l1, "Transcribing") || strcasestr(l1, "Msg received")) {
        return FACE_STATE_THINK;
    }
    if (strcasestr(l1, "Replying") || strcasestr(l1, "Speaking")) {
        return FACE_STATE_SPEAK;
    }
    if (strcasestr(l1, "Snapped") || strcasestr(l1, "Photo Sent") || strstr(l1, "Done:")) {
        return FACE_STATE_HAPPY;
    }
    /* Fall back to raw text for setup, pairing, or diagnostics */
    return FACE_STATE_TEXT;
}

static void display_status_output(const char *line1, const char *line2)
{
    ESP_LOGI(TAG, "[%-22s] %s", line1 ? line1 : "", line2 ? line2 : "");
    if (display_hw_init() != ESP_OK || s_dev == NULL) return;

    face_state_t target_face = map_status_to_face(line1, line2);
    if (target_face != FACE_STATE_TEXT) {
        display_face_set_state(target_face);
        return;
    }

    display_face_set_state(FACE_STATE_TEXT);

    ssd1306_clear_screen(s_dev, 0x00);
    if (line1 && line1[0])
        ssd1306_draw_string(s_dev, 0, 8, (const uint8_t *)line1, 16, 1);
    if (line2 && line2[0])
        ssd1306_draw_string(s_dev, 0, 32, (const uint8_t *)line2, 16, 1);
    if (ssd1306_refresh_gram(s_dev) != ESP_OK) {
        /* Tolerate transient I2C glitches (e.g. EMI from the speaker burst),
         * but stop touching the panel after several consecutive failures. */
        if (++s_refresh_fails >= 5) {
            ESP_LOGE(TAG, "%d consecutive refresh failures — display disabled "
                          "until reboot", s_refresh_fails);
            display_bus_teardown();
            s_display_dead = true;
        } else {
            ESP_LOGW(TAG, "refresh failed — panel not ACKing (%d/5)",
                     s_refresh_fails);
        }
    } else {
        s_refresh_fails = 0;
    }
}
/* ───────────────────────────────────────────────────────────── */

static esp_timer_handle_t s_hold_timer = NULL;
static int64_t s_text_hold_until_us = 0;

static void hold_timer_cb(void *arg)
{
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_text_hold_until_us = 0;
        display_face_set_state(FACE_STATE_IDLE);
        xSemaphoreGive(s_lock);
    } else {
        s_text_hold_until_us = 0;
        display_face_set_state(FACE_STATE_IDLE);
    }
}

void display_status_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (!s_hold_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = &hold_timer_cb,
            .name = "oled_hold_tmr",
        };
        esp_timer_create(&timer_args, &s_hold_timer);
    }
}

void display_status_set(const char *line1, const char *line2)
{
    if (!s_lock || !line1) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (s_hold_timer && esp_timer_is_active(s_hold_timer)) {
        esp_timer_stop(s_hold_timer);
    }
    s_text_hold_until_us = 0;
    strlcpy(s_line1, line1, sizeof(s_line1));
    strlcpy(s_line2, line2 ? line2 : "", sizeof(s_line2));
    display_status_output(s_line1, s_line2);
    xSemaphoreGive(s_lock);
}

void display_status_set_text_timed(const char *line1, const char *line2, uint32_t min_hold_ms)
{
    if (!s_lock || !line1) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (s_hold_timer) {
        esp_timer_stop(s_hold_timer);
        if (min_hold_ms > 0) {
            esp_timer_start_once(s_hold_timer, (uint64_t)min_hold_ms * 1000);
            s_text_hold_until_us = esp_timer_get_time() + ((int64_t)min_hold_ms * 1000);
        } else {
            s_text_hold_until_us = 0;
        }
    }
    strlcpy(s_line1, line1, sizeof(s_line1));
    strlcpy(s_line2, line2 ? line2 : "", sizeof(s_line2));
    display_status_output(s_line1, s_line2);
    xSemaphoreGive(s_lock);
}

void display_status_extend_hold(uint32_t extra_ms)
{
    if (!s_lock) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return;
    if (s_hold_timer && extra_ms > 0) {
        esp_timer_stop(s_hold_timer);
        esp_timer_start_once(s_hold_timer, (uint64_t)extra_ms * 1000);
        s_text_hold_until_us = esp_timer_get_time() + ((int64_t)extra_ms * 1000);
    }
    xSemaphoreGive(s_lock);
}

bool display_status_is_held(void)
{
    return (s_text_hold_until_us > 0 && esp_timer_get_time() < s_text_hold_until_us);
}

void display_status_idle(void)
{
    if (s_text_hold_until_us > 0 && esp_timer_get_time() < s_text_hold_until_us) {
        /* Hold is still active; keep text on screen */
        return;
    }
    if (s_hold_timer && esp_timer_is_active(s_hold_timer)) {
        esp_timer_stop(s_hold_timer);
    }
    s_text_hold_until_us = 0;
    display_face_set_state(FACE_STATE_IDLE);
}
