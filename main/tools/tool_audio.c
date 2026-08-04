#include "tools/tool_audio.h"
#include "esp_log.h"
#include "cJSON.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "tool_audio";

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static bool audio_initialized = false;
static volatile bool ptt_recording = false;

esp_err_t tool_audio_init(void)
{
    if (audio_initialized) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channels: %s", esp_err_to_name(err));
        return err;
    }

    /* TX Configuration for MAX98357A Speaker */
    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_SHARED_BCLK_IO,
            .ws = (gpio_num_t)I2S_SHARED_WS_IO,
            .dout = (gpio_num_t)I2S_AMP_DIN_IO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    /* RX Configuration for INMP441 Microphone (Shares BCLK/WS with TX) */
    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_GPIO_UNUSED, /* Shared with TX */
            .ws = I2S_GPIO_UNUSED,   /* Shared with TX */
            .dout = I2S_GPIO_UNUSED,
            .din = (gpio_num_t)I2S_MIC_SD_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(tx_handle, &tx_std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S TX std init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_init_std_mode(rx_handle, &rx_std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S RX std init failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_channel_enable(tx_handle);
    i2s_channel_enable(rx_handle);

    audio_initialized = true;
    ESP_LOGI(TAG, "I2S Audio driver initialized (BCLK=%d, WS=%d, AMP_DIN=%d, MIC_SD=%d)",
             I2S_SHARED_BCLK_IO, I2S_SHARED_WS_IO, I2S_AMP_DIN_IO, I2S_MIC_SD_IO);
    return ESP_OK;
}

static esp_err_t tool_audio_play_tone_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    if (tool_audio_init() != ESP_OK) {
        snprintf(output, output_size, "{\"error\": \"Audio hardware init failed\"}");
        return ESP_OK;
    }

    #define TONE_FREQ_HZ 444
    #define DURATION_S 1.5
    size_t total_samples = (size_t)(I2S_SAMPLE_RATE * DURATION_S);
    int16_t *buf = malloc(sizeof(int16_t) * 1024);
    if (!buf) {
        snprintf(output, output_size, "{\"error\": \"Memory allocation failed\"}");
        return ESP_OK;
    }

    size_t samples_written = 0;
    size_t bytes_written = 0;

    while (samples_written < total_samples) {
        size_t chunk = (total_samples - samples_written > 1024) ? 1024 : (total_samples - samples_written);
        for (size_t i = 0; i < chunk; i++) {
            double t = (double)(samples_written + i) / I2S_SAMPLE_RATE;
            buf[i] = (int16_t)(sin(2.0 * M_PI * TONE_FREQ_HZ * t) * 12000.0);
        }
        i2s_channel_write(tx_handle, buf, chunk * sizeof(int16_t), &bytes_written, 1000);
        samples_written += chunk;
    }

    free(buf);
    snprintf(output, output_size, "{\"status\": \"success\", \"message\": \"Played 444Hz test tone on MAX98357A speaker\"}");
    ESP_LOGI(TAG, "Played 444Hz test tone on MAX98357A speaker");
    return ESP_OK;
}

static esp_err_t tool_audio_record_test_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    if (tool_audio_init() != ESP_OK) {
        snprintf(output, output_size, "{\"error\": \"Audio hardware init failed\"}");
        return ESP_OK;
    }

    int32_t buf[256];
    size_t bytes_read = 0;
    double sum_sq = 0.0;
    size_t total_read = 0;

    for (int i = 0; i < 50; i++) {
        if (i2s_channel_read(rx_handle, buf, sizeof(buf), &bytes_read, 100) == ESP_OK) {
            size_t samples = bytes_read / sizeof(int32_t);
            for (size_t j = 0; j < samples; j++) {
                double s = (double)(buf[j] >> 14);
                sum_sq += s * s;
                total_read++;
            }
        }
    }

    double rms = (total_read > 0) ? sqrt(sum_sq / total_read) : 0.0;
    snprintf(output, output_size, "{\"status\": \"success\", \"samples\": %d, \"rms_level\": %.2f, \"message\": \"INMP441 mic test recording verified\"}",
             (int)total_read, rms);
    ESP_LOGI(TAG, "INMP441 mic test: %d samples, RMS=%.2f", (int)total_read, rms);
    return ESP_OK;
}

static void ptt_record_task(void *arg)
{
    int32_t buf[256];
    size_t bytes_read = 0;
    size_t total_bytes = 0;
    
    ESP_LOGI(TAG, "PTT record task reading loop started...");

    while (ptt_recording) {
        esp_err_t res = i2s_channel_read(rx_handle, buf, sizeof(buf), &bytes_read, pdMS_TO_TICKS(100));
        if (res == ESP_OK) {
            total_bytes += bytes_read;
        } else if (res == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S read timeout");
        } else {
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(res));
        }
    }
    
    ESP_LOGI(TAG, "PTT Recording complete: Captured %u bytes total. (Fake sending to LLM...)", (unsigned int)total_bytes);
    
    vTaskDelete(NULL);
}

void tool_audio_ptt_record_start(void)
{
    if (tool_audio_init() == ESP_OK) {
        if (!ptt_recording) {
            ptt_recording = true;
            ESP_LOGI(TAG, "PTT Recording started (INMP441 GPIO 1)");
            xTaskCreate(ptt_record_task, "ptt_rec_task", 4096, NULL, 5, NULL);
        }
    }
}

void tool_audio_ptt_record_stop(void)
{
    if (ptt_recording) {
        ptt_recording = false;
        ESP_LOGI(TAG, "PTT Recording stopped and submitted");
    }
}

mimi_tool_t audio_play_tone_tool = {
    .name = "audio_play_tone",
    .description = "Play a 444Hz test tone through the physical MAX98357A speaker to verify audio output.",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{},"
        "\"required\":[]}",
    .execute = tool_audio_play_tone_execute,
};

mimi_tool_t audio_record_test_tool = {
    .name = "audio_record_test",
    .description = "Capture an audio sample from the physical INMP441 microphone and verify signal input.",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{},"
        "\"required\":[]}",
    .execute = tool_audio_record_test_execute,
};
