#include "tools/tool_gpio.h"
#include "tools/gpio_policy.h"
#include "mimi_config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "tool_gpio";

esp_err_t tool_gpio_init(void)
{
    ESP_LOGI(TAG, "GPIO tool initialized (setting output pins to OFF on 1st init)");

    /* Reset and set output GPIO pins (White LED pin 40, etc.) to LOW/OFF */
    const int output_pins[] = { MIMI_GPIO_LED_WHITE };
    for (size_t i = 0; i < sizeof(output_pins) / sizeof(output_pins[0]); i++) {
        int pin = output_pins[i];
        if (gpio_policy_pin_is_allowed(pin)) {
            gpio_reset_pin((gpio_num_t)pin);
            gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
            gpio_set_level((gpio_num_t)pin, 0);
            ESP_LOGI(TAG, "GPIO %d initialized to OFF (LOW)", pin);
        }
    }

    return ESP_OK;
}

esp_err_t tool_gpio_write_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *pin_obj = cJSON_GetObjectItem(root, "pin");
    cJSON *state_obj = cJSON_GetObjectItem(root, "state");

    if (!cJSON_IsNumber(pin_obj)) {
        snprintf(output, output_size, "Error: 'pin' required (integer)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsNumber(state_obj)) {
        snprintf(output, output_size, "Error: 'state' required (0 or 1)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int pin = (int)pin_obj->valuedouble;
    int state = (int)state_obj->valuedouble;

    if (!gpio_policy_pin_is_allowed(pin)) {
        if (gpio_policy_pin_forbidden_hint(pin, output, output_size)) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        if (MIMI_GPIO_ALLOWED_CSV[0] != '\0') {
            snprintf(output, output_size, "Error: pin %d is not in allowed list", pin);
        } else {
            snprintf(output, output_size, "Error: pin must be %d-%d",
                     MIMI_GPIO_MIN_PIN, MIMI_GPIO_MAX_PIN);
        }
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT) != ESP_OK ||
        gpio_set_level(pin, state ? 1 : 0) != ESP_OK) {
        snprintf(output, output_size, "Error: failed to configure/write pin %d", pin);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    snprintf(output, output_size, "Pin %d set to %s", pin, state ? "HIGH" : "LOW");
    ESP_LOGI(TAG, "gpio_write: pin %d -> %s", pin, state ? "HIGH" : "LOW");

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_gpio_read_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *pin_obj = cJSON_GetObjectItem(root, "pin");
    if (!cJSON_IsNumber(pin_obj)) {
        snprintf(output, output_size, "Error: 'pin' required (integer)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int pin = (int)pin_obj->valuedouble;

    if (!gpio_policy_pin_is_allowed(pin)) {
        if (gpio_policy_pin_forbidden_hint(pin, output, output_size)) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        if (MIMI_GPIO_ALLOWED_CSV[0] != '\0') {
            snprintf(output, output_size, "Error: pin %d is not in allowed list", pin);
        } else {
            snprintf(output, output_size, "Error: pin must be %d-%d",
                     MIMI_GPIO_MIN_PIN, MIMI_GPIO_MAX_PIN);
        }
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    /* Enable input path with pull-up for buttons, then read level */
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    if (pin == 38 || pin == 39) {
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
    }
    int level = gpio_get_level(pin);

    snprintf(output, output_size, "Pin %d = %s", pin, level ? "HIGH" : "LOW");
    ESP_LOGI(TAG, "gpio_read: pin %d = %s", pin, level ? "HIGH" : "LOW");

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_gpio_read_all_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    char *cursor = output;
    size_t remaining = output_size;
    int written;
    int count = 0;

    written = snprintf(cursor, remaining, "GPIO states: ");
    if (written < 0 || (size_t)written >= remaining) {
        output[0] = '\0';
        return ESP_FAIL;
    }
    cursor += (size_t)written;
    remaining -= (size_t)written;

    if (MIMI_GPIO_ALLOWED_CSV[0] != '\0') {
        /* Iterate over explicit allowlist */
        const char *csv_cursor = MIMI_GPIO_ALLOWED_CSV;
        while (*csv_cursor != '\0') {
            char *endptr = NULL;
            long value;

            while (*csv_cursor == ' ' || *csv_cursor == '\t' || *csv_cursor == ',') {
                csv_cursor++;
            }
            if (*csv_cursor == '\0') break;

            value = strtol(csv_cursor, &endptr, 10);
            if (endptr == csv_cursor) {
                while (*csv_cursor != '\0' && *csv_cursor != ',') csv_cursor++;
                continue;
            }
            if (!gpio_policy_pin_is_allowed((int)value)) {
                csv_cursor = endptr;
                continue;
            }

            gpio_set_direction((int)value, GPIO_MODE_INPUT);
            if ((int)value == 38 || (int)value == 39) {
                gpio_set_pull_mode((gpio_num_t)value, GPIO_PULLUP_ONLY);
            }
            int level = gpio_get_level((int)value);

            written = snprintf(cursor, remaining, "%s%d=%s",
                               count == 0 ? "" : ", ",
                               (int)value, level ? "HIGH" : "LOW");
            if (written < 0 || (size_t)written >= remaining) break;
            cursor += (size_t)written;
            remaining -= (size_t)written;
            count++;
            csv_cursor = endptr;
        }
    } else {
        /* Iterate over default range */
        for (int pin = MIMI_GPIO_MIN_PIN; pin <= MIMI_GPIO_MAX_PIN; pin++) {
            if (!gpio_policy_pin_is_allowed(pin)) continue;

            gpio_set_direction(pin, GPIO_MODE_INPUT);
            int level = gpio_get_level(pin);

            written = snprintf(cursor, remaining, "%s%d=%s",
                               count == 0 ? "" : ", ",
                               pin, level ? "HIGH" : "LOW");
            if (written < 0 || (size_t)written >= remaining) break;
            cursor += (size_t)written;
            remaining -= (size_t)written;
            count++;
        }
    }

    if (count == 0) {
        snprintf(output, output_size, "Error: no allowed GPIO pins configured");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "gpio_read_all: %d pins read", count);
    return ESP_OK;
}
