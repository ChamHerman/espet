#pragma once

#include "esp_err.h"

/**
 * Initialize the Telegram bot.
 */
esp_err_t telegram_bot_init(void);

/**
 * Start the Telegram polling task (long polling on Core 0).
 */
esp_err_t telegram_bot_start(void);

/**
 * Send a text message to a Telegram chat.
 * Automatically splits messages longer than 4096 chars.
 * @param chat_id  Telegram chat ID (numeric string)
 * @param text     Message text (supports Markdown)
 */
esp_err_t telegram_send_message(const char *chat_id, const char *text);

/**
 * Save the Telegram bot token to NVS.
 */
esp_err_t telegram_set_token(const char *token);

/**
 * Send a text message to the most recent active Telegram chat.
 */
esp_err_t telegram_send_to_last_chat(const char *text);

/**
 * Set and persist the last active or bound Telegram chat ID.
 */
void telegram_set_last_chat_id(const char *chat_id);

/**
 * Get the last active or bound Telegram chat ID.
 */
const char *telegram_get_last_chat_id(void);

/**
 * Send a photo to a Telegram chat using multipart/form-data.
 * @param chat_id    Telegram chat ID
 * @param jpeg_data  Pointer to the JPEG bytes
 * @param jpeg_len   Length of the JPEG bytes
 * @param caption    Optional text caption (can be NULL)
 */
esp_err_t telegram_send_photo(const char *chat_id, const uint8_t *jpeg_data, size_t jpeg_len, const char *caption);

/**
 * Send a photo to the most recent active Telegram chat.
 */
esp_err_t telegram_send_photo_to_last_chat(const uint8_t *jpeg_data, size_t jpeg_len, const char *caption);

/**
 * Send a video file to a Telegram chat using multipart/form-data.
 * @param chat_id   Telegram chat ID
 * @param video_data Pointer to the video bytes (e.g. MJPEG AVI)
 * @param video_len  Length of the video bytes
 * @param caption    Optional text caption (can be NULL)
 */
esp_err_t telegram_send_video(const char *chat_id, const uint8_t *video_data, size_t video_len, const char *caption);

/**
 * Send a video file to the most recent active Telegram chat.
 */
esp_err_t telegram_send_video_to_last_chat(const uint8_t *video_data, size_t video_len, const char *caption);


