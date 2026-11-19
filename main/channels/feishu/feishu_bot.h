#pragma once

#include "esp_err.h"
#include "mimi_config.h"

#if MIMI_FEISHU_ENABLE

/**
 * Initialize the Feishu bot (load credentials from NVS / build-time).
 */
esp_err_t feishu_bot_init(void);

/**
 * Start the Feishu WebSocket connection for receiving events.
 */
esp_err_t feishu_bot_start(void);

/**
 * Send a text message to a Feishu chat.
 * Automatically splits messages longer than MIMI_FEISHU_MAX_MSG_LEN chars.
 * @param chat_id  Feishu chat ID (open_id or chat_id)
 * @param text     Message text
 */
esp_err_t feishu_send_message(const char *chat_id, const char *text);

/**
 * Reply to a specific message in a Feishu chat.
 * @param message_id  The message_id to reply to
 * @param text        Reply text
 */
esp_err_t feishu_reply_message(const char *message_id, const char *text);

/**
 * Save Feishu app credentials to NVS.
 * @param app_id     Feishu App ID
 * @param app_secret Feishu App Secret
 */
esp_err_t feishu_set_credentials(const char *app_id, const char *app_secret);

#else

/* Inline no-op / disabled stubs when Feishu channel is compiled out */
static inline esp_err_t feishu_bot_init(void) { return ESP_OK; }
static inline esp_err_t feishu_bot_start(void) { return ESP_OK; }
static inline esp_err_t feishu_send_message(const char *chat_id, const char *text) {
    (void)chat_id; (void)text;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t feishu_reply_message(const char *message_id, const char *text) {
    (void)message_id; (void)text;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t feishu_set_credentials(const char *app_id, const char *app_secret) {
    (void)app_id; (void)app_secret;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
