#include "tool_oled.h"
#include "cJSON.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "tool_oled";

// Default I2C configuration for ESP32-S3 (can be changed based on wiring)
#define I2C_MASTER_SCL_IO 21
#define I2C_MASTER_SDA_IO 47
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

static ssd1306_handle_t ssd1306_dev = NULL;
static bool oled_initialized = false;

static esp_err_t i2c_master_init(void) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = I2C_MASTER_FREQ_HZ,
  };

  esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
  if (err != ESP_OK) {
    return err;
  }

  return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t init_oled(void) {
  if (oled_initialized) {
    return ESP_OK;
  }

  esp_err_t err = i2c_master_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "I2C initialized failed: %s", esp_err_to_name(err));
    return err;
  }

  ssd1306_dev = ssd1306_create(I2C_MASTER_NUM, SSD1306_I2C_ADDRESS);
  if (ssd1306_dev == NULL) {
    ESP_LOGE(TAG, "SSD1306 create failed");
    return ESP_FAIL;
  }

  ssd1306_clear_screen(ssd1306_dev, 0x00);
  ssd1306_refresh_gram(ssd1306_dev);

  oled_initialized = true;
  ESP_LOGI(TAG, "OLED initialized successfully");
  return ESP_OK;
}

void oled_display_status(const char *line1, const char *line2) {
  if (init_oled() != ESP_OK) {
    return;
  }
  ssd1306_clear_screen(ssd1306_dev, 0x00);
  if (line1 && line1[0]) {
    char buf1[17];
    snprintf(buf1, sizeof(buf1), "%.16s", line1);
    ssd1306_draw_string(ssd1306_dev, 0, 0, (const uint8_t *)buf1, 16, 1);
  }
  if (line2 && line2[0]) {
    char buf2[17];
    snprintf(buf2, sizeof(buf2), "%.16s", line2);
    ssd1306_draw_string(ssd1306_dev, 0, 24, (const uint8_t *)buf2, 16, 1);
  }
  ssd1306_refresh_gram(ssd1306_dev);
}

static esp_err_t tool_oled_print_execute(const char *input_json, char *output,
                                         size_t output_size) {
  cJSON *root = cJSON_Parse(input_json);
  if (!root) {
    snprintf(output, output_size, "{\"error\": \"Invalid JSON\"}");
    return ESP_OK;
  }

  cJSON *text_item = cJSON_GetObjectItem(root, "text");
  if (cJSON_IsString(text_item)) {
    if (init_oled() == ESP_OK) {
      // Clear screen
      ssd1306_clear_screen(ssd1306_dev, 0x00);

      // Draw string (handle, x, y, string, font_size, color)
      ssd1306_draw_string(ssd1306_dev, 0, 0,
                          (const uint8_t *)text_item->valuestring, 16, 1);

      // Refresh to show changes
      ssd1306_refresh_gram(ssd1306_dev);

      snprintf(
          output, output_size,
          "{\"status\": \"success\", \"message\": \"Text displayed on OLED\"}");
    } else {
      snprintf(output, output_size,
               "{\"error\": \"OLED initialization failed\"}");
    }
  } else {
    snprintf(output, output_size, "{\"error\": \"Missing text parameter\"}");
  }

  cJSON_Delete(root);
  return ESP_OK;
}

mimi_tool_t oled_tool = {
    .name = "oled_print",
    .description =
        "Display a short text message on the physical SSD1306 OLED screen.",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"text\":{\"type\":\"string\",\"description\":\"The "
        "message to display on the screen\"}},"
        "\"required\":[\"text\"]}",
    .execute = tool_oled_print_execute,
};
