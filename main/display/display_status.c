#include "display_status.h"

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

static const char *TAG = "display";

#define DISPLAY_I2C_PORT   I2C_NUM_0
#define DISPLAY_I2C_FREQ   400000
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
 * SCL=2, SDA=1 via MIMI_GPIO_OLED_SCL/SDA. There is deliberately NO pin
 * hunting — probing other GPIOs pulsed the camera button (GPIO 41) and the
 * LED (42), caused phantom button events, and stalled every display update
 * with a 25-pair I2C scan. If the panel does not respond here, the display
 * is disabled until reboot instead of re-hunting. */
static const uint8_t s_oled_addrs[2] = {0x3C, 0x3D};

static int s_found_addr = -1;
static bool s_bus_installed;
static bool s_display_dead;      /* panel unavailable — no I2C until reboot */
static int  s_refresh_fails;

/* Configure the port on the configured (scl, sda) pin pair and probe both
 * OLED addresses. */
static bool probe_pin_pair(int scl, int sda)
{
    if (s_bus_installed) {
        i2c_driver_delete(DISPLAY_I2C_PORT);
        s_bus_installed = false;
    }

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

    uint8_t probe = 0x00;
    for (int a = 0; a < 2; a++) {
        /* require two consecutive ACKs — a single flaky ACK on a floating
         * pin must not derail the probe */
        bool ack1 = i2c_master_write_to_device(DISPLAY_I2C_PORT, s_oled_addrs[a], &probe, 1,
                                               pdMS_TO_TICKS(20)) == ESP_OK;
        if (!ack1) continue;
        bool ack2 = i2c_master_write_to_device(DISPLAY_I2C_PORT, s_oled_addrs[a], &probe, 1,
                                               pdMS_TO_TICKS(20)) == ESP_OK;
        if (ack2) {
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

    ESP_LOGI(TAG, "Probing OLED on configured pins (SCL=%d, SDA=%d)...",
             MIMI_GPIO_OLED_SCL, MIMI_GPIO_OLED_SDA);
    if (!probe_pin_pair(MIMI_GPIO_OLED_SCL, MIMI_GPIO_OLED_SDA)) {
        ESP_LOGE(TAG, "OLED not ACKing on configured pins SCL=%d/SDA=%d — panel "
                      "dead or disconnected. Display disabled until reboot.",
                 MIMI_GPIO_OLED_SCL, MIMI_GPIO_OLED_SDA);
        display_bus_teardown();
        s_display_dead = true;
        return ESP_FAIL;
    }

    s_dev = ssd1306_create(DISPLAY_I2C_PORT, s_found_addr);
    if (s_dev == NULL) {
        ESP_LOGE(TAG, "SSD1306 create failed");
        display_bus_teardown();
        s_display_dead = true;
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
    return ESP_OK;
}

static void display_status_output(const char *line1, const char *line2)
{
    ESP_LOGI(TAG, "[%-22s] %s", line1 ? line1 : "", line2 ? line2 : "");
    if (display_hw_init() != ESP_OK || s_dev == NULL) return;

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

void display_status_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
}

void display_status_set(const char *line1, const char *line2)
{
    if (!s_lock || !line1) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return;
    strlcpy(s_line1, line1, sizeof(s_line1));
    strlcpy(s_line2, line2 ? line2 : "", sizeof(s_line2));
    display_status_output(s_line1, s_line2);
    xSemaphoreGive(s_lock);
}

void display_status_idle(void)
{
    display_status_set("Hi ESP to wake", "");
}
