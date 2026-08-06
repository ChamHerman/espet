#include "tool_oled.h"
#include "cJSON.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "oled_bitmaps.h"

static const char *TAG = "tool_oled";

#define I2C_MASTER_SCL_IO 21
#define I2C_MASTER_SDA_IO 47
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

static ssd1306_handle_t ssd1306_dev = NULL;
static bool oled_initialized = false;

// --- State Machine ---
typedef enum {
    OLED_STATE_IDLE_EYES,
    OLED_STATE_THINKING,
    OLED_STATE_DONE,
    OLED_STATE_TEXT
} oled_anim_state_t;

static oled_anim_state_t current_state = OLED_STATE_IDLE_EYES;
static int64_t state_revert_time_us = 0;

static char current_display_text[32] = {0};

// --- Idle Eye State Variables ---
static const int ref_eye_height = 40;
static const int ref_eye_width = 40;
static const int ref_space_between_eye = 10;

static int left_eye_height = 40;
static int left_eye_width = 40;
static int left_eye_x = 32;
static int left_eye_y = 32;

static int right_eye_x = 32 + 40 + 10;
static int right_eye_y = 32;
static int right_eye_height = 40;
static int right_eye_width = 40;

static int current_eye_anim_index = 0;
static const int max_eye_anim_index = 8;

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
  if (err != ESP_OK) return err;
  return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t init_oled(void) {
  if (oled_initialized) return ESP_OK;
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

// --- Animation Functions (Idle Eyes) ---
static void draw_eyes(bool update) {
    if (!oled_initialized) return;
    ssd1306_clear_screen(ssd1306_dev, 0x00);
    int x1 = left_eye_x - left_eye_width / 2;
    int y1 = left_eye_y - left_eye_height / 2;
    ssd1306_fill_rectangle(ssd1306_dev, x1, y1, x1 + left_eye_width, y1 + left_eye_height, 1);
    int x2 = right_eye_x - right_eye_width / 2;
    int y2 = right_eye_y - right_eye_height / 2;
    ssd1306_fill_rectangle(ssd1306_dev, x2, y2, x2 + right_eye_width, y2 + right_eye_height, 1);
    if (update) ssd1306_refresh_gram(ssd1306_dev);
}

static void center_eyes(bool update) {
    left_eye_height = ref_eye_height;
    left_eye_width = ref_eye_width;
    right_eye_height = ref_eye_height;
    right_eye_width = ref_eye_width;
    left_eye_x = SCREEN_WIDTH / 2 - ref_eye_width / 2 - ref_space_between_eye / 2;
    left_eye_y = SCREEN_HEIGHT / 2;
    right_eye_x = SCREEN_WIDTH / 2 + ref_eye_width / 2 + ref_space_between_eye / 2;
    right_eye_y = SCREEN_HEIGHT / 2;
    draw_eyes(update);
}

static void sleep_anim(void) {
    left_eye_height = 2;
    right_eye_height = 2;
    draw_eyes(true);
}

static void wakeup_anim(void) {
    sleep_anim();
    for (int h = 0; h <= ref_eye_height; h += 4) {
        left_eye_height = h;
        right_eye_height = h;
        if (current_state != OLED_STATE_IDLE_EYES) return;
        draw_eyes(true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void blink(int speed) {
    draw_eyes(true);
    for (int i = 0; i < 3; i++) {
        left_eye_height -= speed;
        right_eye_height -= speed;
        if (left_eye_height < 2) left_eye_height = 2;
        if (right_eye_height < 2) right_eye_height = 2;
        if (current_state != OLED_STATE_IDLE_EYES) return;
        draw_eyes(true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (int i = 0; i < 3; i++) {
        left_eye_height += speed;
        right_eye_height += speed;
        if (left_eye_height > ref_eye_height) left_eye_height = ref_eye_height;
        if (right_eye_height > ref_eye_height) right_eye_height = ref_eye_height;
        if (current_state != OLED_STATE_IDLE_EYES) return;
        draw_eyes(true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void saccade(int direction_x, int direction_y) {
    int dir_x_amp = 8, dir_y_amp = 6, blink_amp = 8;
    left_eye_x += dir_x_amp * direction_x;
    right_eye_x += dir_x_amp * direction_x;
    left_eye_y += dir_y_amp * direction_y;
    right_eye_y += dir_y_amp * direction_y;
    right_eye_height -= blink_amp;
    left_eye_height -= blink_amp;
    draw_eyes(true);
    vTaskDelay(pdMS_TO_TICKS(10));
    left_eye_x += dir_x_amp * direction_x;
    right_eye_x += dir_x_amp * direction_x;
    left_eye_y += dir_y_amp * direction_y;
    right_eye_y += dir_y_amp * direction_y;
    right_eye_height += blink_amp;
    left_eye_height += blink_amp;
    if (current_state != OLED_STATE_IDLE_EYES) return;
    draw_eyes(true);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void move_big_eye(int direction) {
    int dir_oversize = 2, dir_move_amp = 2, blink_amp = 5;
    for (int i = 0; i < 3; i++) {
        left_eye_x += dir_move_amp * direction;
        right_eye_x += dir_move_amp * direction;
        right_eye_height -= blink_amp;
        left_eye_height -= blink_amp;
        if (direction > 0) { right_eye_height += dir_oversize; right_eye_width += dir_oversize; }
        else { left_eye_height += dir_oversize; left_eye_width += dir_oversize; }
        if (current_state != OLED_STATE_IDLE_EYES) return;
        draw_eyes(true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (int i = 0; i < 3; i++) {
        left_eye_x += dir_move_amp * direction;
        right_eye_x += dir_move_amp * direction;
        right_eye_height += blink_amp;
        left_eye_height += blink_amp;
        if (direction > 0) { right_eye_height += dir_oversize; right_eye_width += dir_oversize; }
        else { left_eye_height += dir_oversize; left_eye_width += dir_oversize; }
        if (current_state != OLED_STATE_IDLE_EYES) return;
        draw_eyes(true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    if (current_state != OLED_STATE_IDLE_EYES) return;
    for (int i = 0; i < 3; i++) {
        left_eye_x -= dir_move_amp * direction;
        right_eye_x -= dir_move_amp * direction;
        right_eye_height -= blink_amp;
        left_eye_height -= blink_amp;
        if (direction > 0) { right_eye_height -= dir_oversize; right_eye_width -= dir_oversize; }
        else { left_eye_height -= dir_oversize; left_eye_width -= dir_oversize; }
        draw_eyes(true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (int i = 0; i < 3; i++) {
        left_eye_x -= dir_move_amp * direction;
        right_eye_x -= dir_move_amp * direction;
        right_eye_height += blink_amp;
        left_eye_height += blink_amp;
        if (direction > 0) { right_eye_height -= dir_oversize; right_eye_width -= dir_oversize; }
        else { left_eye_height -= dir_oversize; left_eye_width -= dir_oversize; }
        draw_eyes(true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    center_eyes(true);
}

static void launch_eye_animation_with_index(int idx) {
    if (idx > max_eye_anim_index) idx = 8;
    switch (idx) {
        case 0: wakeup_anim(); break;
        case 1: center_eyes(true); break;
        case 2: move_big_eye(1); break;
        case 3: move_big_eye(-1); break;
        case 4: blink(10); break;
        case 5: blink(20); break;
        case 6: center_eyes(true); break;
        case 7: sleep_anim(); break;
        case 8:
            center_eyes(true);
            for (int i = 0; i < 10; i++) {
                if (current_state != OLED_STATE_IDLE_EYES) return;
                int dir_x = (rand() % 3) - 1;
                int dir_y = (rand() % 3) - 1;
                saccade(dir_x, dir_y);
                vTaskDelay(pdMS_TO_TICKS(50));
                saccade(-dir_x, -dir_y);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            break;
    }
}

// --- Main Task ---
static void oled_anim_task(void *arg) {
    init_oled();
    center_eyes(true);
    
    int loading_frame_idx = 0;
    int done_frame_idx = 0;
    oled_anim_state_t prev_state = current_state;

    while (1) {
        int64_t now = esp_timer_get_time();
        
        // Revert temporary states (DONE or TEXT) after 5 seconds
        if (state_revert_time_us > 0 && now >= state_revert_time_us) {
            current_state = OLED_STATE_IDLE_EYES;
            state_revert_time_us = 0;
        }

        // Detect state changes to reset animations
        if (current_state != prev_state) {
            loading_frame_idx = 0;
            done_frame_idx = 0;
            if (oled_initialized) {
                ssd1306_clear_screen(ssd1306_dev, 0x00);
                ssd1306_refresh_gram(ssd1306_dev);
            }
            if (current_state == OLED_STATE_IDLE_EYES) {
                center_eyes(true);
                current_eye_anim_index = 0;
            }
            prev_state = current_state;
        }

        if (current_state == OLED_STATE_IDLE_EYES) {
            launch_eye_animation_with_index(current_eye_anim_index++);
            if (current_eye_anim_index > max_eye_anim_index) current_eye_anim_index = 0;
            vTaskDelay(pdMS_TO_TICKS(500));
        } 
        else if (current_state == OLED_STATE_THINKING) {
            if (oled_initialized) {
                ssd1306_clear_screen(ssd1306_dev, 0x00);
                ssd1306_draw_bitmap(ssd1306_dev, (SCREEN_WIDTH - FRAME_WIDTH) / 2, (SCREEN_HEIGHT - FRAME_HEIGHT) / 2, loading_frames[loading_frame_idx], FRAME_WIDTH, FRAME_HEIGHT);
                ssd1306_refresh_gram(ssd1306_dev);
            }
            if (loading_frame_idx < LOADING_FRAME_COUNT - 1) {
                loading_frame_idx++;
                vTaskDelay(pdMS_TO_TICKS(42));
            } else {
                vTaskDelay(pdMS_TO_TICKS(100)); // hold the last frame
            }
        }
        else if (current_state == OLED_STATE_DONE) {
            if (oled_initialized) {
                ssd1306_clear_screen(ssd1306_dev, 0x00);
                ssd1306_draw_bitmap(ssd1306_dev, (SCREEN_WIDTH - FRAME_WIDTH) / 2, (SCREEN_HEIGHT - FRAME_HEIGHT) / 2, done_frames[done_frame_idx], FRAME_WIDTH, FRAME_HEIGHT);
                ssd1306_refresh_gram(ssd1306_dev);
            }
            if (done_frame_idx < DONE_FRAME_COUNT - 1) {
                done_frame_idx++;
                vTaskDelay(pdMS_TO_TICKS(42));
            } else {
                vTaskDelay(pdMS_TO_TICKS(100)); // hold the last frame
            }
        }
        else if (current_state == OLED_STATE_TEXT) {
            if (oled_initialized) {
                ssd1306_clear_screen(ssd1306_dev, 0x00);
                char buf1[17] = {0};
                char buf2[17] = {0};
                snprintf(buf1, sizeof(buf1), "%.16s", current_display_text);
                if (strlen(current_display_text) > 16) {
                    snprintf(buf2, sizeof(buf2), "%.16s", current_display_text + 16);
                }
                ssd1306_draw_string(ssd1306_dev, 0, 16, (const uint8_t *)buf1, 16, 1);
                ssd1306_draw_string(ssd1306_dev, 0, 32, (const uint8_t *)buf2, 16, 1);
                ssd1306_refresh_gram(ssd1306_dev);
            }
            vTaskDelay(pdMS_TO_TICKS(100)); // sleep a bit while displaying text
        }
    }
}

void tool_oled_init_animation(void) {
    xTaskCreate(oled_anim_task, "oled_anim", 4096, NULL, 5, NULL);
}

void oled_display_status(const char *line1, const char *line2) {
    // Intercept generic status calls
    if (line2 && strstr(line2, "Thinking") != NULL) {
        current_state = OLED_STATE_THINKING;
        state_revert_time_us = 0; // Stays thinking until explicitly changed
    } else if (line1 && strstr(line1, "MimiClaw Ready") != NULL) {
        current_state = OLED_STATE_IDLE_EYES;
        state_revert_time_us = 0;
    } else {
        // Fallback text display
        char merged[32] = {0};
        snprintf(merged, sizeof(merged), "%.16s%.16s", line1 ? line1 : "", line2 ? line2 : "");
        strncpy(current_display_text, merged, sizeof(current_display_text) - 1);
        current_state = OLED_STATE_TEXT;
        state_revert_time_us = esp_timer_get_time() + (5000000LL); // 5 seconds
    }
}

static esp_err_t tool_oled_print_execute(const char *input_json, char *output, size_t output_size) {
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "{\"error\": \"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *text_item = cJSON_GetObjectItem(root, "text");
    if (cJSON_IsString(text_item)) {
        // As per requirements, when the LLM triggers oled_print (a done action), we show the DONE animation instead of text.
        current_state = OLED_STATE_DONE;
        state_revert_time_us = esp_timer_get_time() + (5000000LL); // Show done anim for 5 seconds
        snprintf(output, output_size, "{\"status\": \"success\", \"message\": \"Done animation displayed on OLED for 5s\"}");
    } else {
        snprintf(output, output_size, "{\"error\": \"Missing text parameter\"}");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

mimi_tool_t oled_tool = {
    .name = "oled_print",
    .description = "Display a short text message on the physical SSD1306 OLED screen.",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{\"text\":{\"type\":\"string\",\"description\":\"The "
        "message to display on the screen\"}},"
        "\"required\":[\"text\"]}",
    .execute = tool_oled_print_execute,
};
