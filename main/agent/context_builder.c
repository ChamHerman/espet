#include "context_builder.h"
#include "mimi_config.h"
#include "memory/memory_store.h"
#include "skills/skill_loader.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "context";

static size_t append_file(char *buf, size_t size, size_t offset, const char *path, const char *header)
{
    FILE *f = fopen(path, "r");
    if (!f) return offset;

    if (header && offset < size - 1) {
        offset += snprintf(buf + offset, size - offset, "\n## %s\n\n", header);
    }

    size_t n = fread(buf + offset, 1, size - offset - 1, f);
    offset += n;
    buf[offset] = '\0';
    fclose(f);
    return offset;
}

esp_err_t context_build_system_prompt(char *buf, size_t size)
{
    size_t off = 0;

    off += snprintf(buf + off, size - off,
        "# ESPET\n\n"
        "You are ESPET, an embodied AI agent running on an ESP32-S3 microcontroller.\n"
        "Your behavior, tone, user context, and working memory are defined below.\n"
        "Use available tools to perceive the physical world and execute actions.\n\n"
        "## Hardware & Environment\n"
        "- White LED: GPIO 42 (use gpio_write: state 1=ON, 0=OFF)\n"
        "- Green Button: GPIO 41 (use gpio_read: state 0=Pressed, 1=Released)\n"
        "- File paths: all SPIFFS operations must start with /spiffs/\n"
        "- Current Time: call get_current_time when current date or time is needed\n\n");

    /* Bootstrap files */
    off = append_file(buf, size, off, MIMI_SOUL_FILE, "Personality & Behavior");
    off = append_file(buf, size, off, MIMI_USER_FILE, "User Profile");

    /* Working memory */
    char mem_buf[4096];
    if (memory_read_long_term(mem_buf, sizeof(mem_buf)) == ESP_OK && mem_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Working Memory\n\n%s\n", mem_buf);
    }

    /* Recent daily notes (last 3 days) */
    char recent_buf[4096];
    if (memory_read_recent(recent_buf, sizeof(recent_buf), 3) == ESP_OK && recent_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Recent Notes\n\n%s\n", recent_buf);
    }

    /* Skills — 3 KB so a 16-skill catalog isn't silently truncated */
    char skills_buf[3072];
    size_t skills_len = skill_loader_build_summary(skills_buf, sizeof(skills_buf));
    if (skills_len > 0) {
        off += snprintf(buf + off, size - off,
            "\n## Available Skills\n\n"
            "Available skills (use read_file to load full instructions):\n%s\n",
            skills_buf);
    }

    ESP_LOGI(TAG, "System prompt built: %d bytes", (int)off);
    return ESP_OK;
}
