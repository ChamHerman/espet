/*
 * cloud_link.h — hybrid-mode cloud channel (Plan C-Hybrid)
 *
 * One persistent outbound WebSocket connection to the companion backend
 * (ws://<backend>:8000/ws/device?key=<DEVICE_API_KEY>). The backend polls
 * Telegram for the device (single poller) and pushes `message_in` frames;
 * the device sends replies as `message_out` frames.
 *
 * Protocol v1 (docs/HYBRID_MODE_PLAN.md §2):
 *   device → cloud : hello, message_out, photo_out, ping
 *   cloud  → device: hello_ack, message_in, pong, error
 *
 * When MIMI_CLOUD_LINK_ENABLE is 1 the device does NOT start
 * telegram_bot polling (the backend owns the bot token); the agent loop,
 * memory and tools are untouched.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the cloud link: resolve the backend URL, open the WebSocket
 *        and register the frame handler. Auto-reconnects on drop.
 *        Sends the `hello` handshake on every (re)connect.
 */
esp_err_t cloud_link_start(void);

/**
 * @brief Stop the cloud link and close the WebSocket.
 */
void cloud_link_stop(void);

/**
 * @brief Send an agent reply to a chat through the cloud gateway
 *        (frame: message_out).
 * @return ESP_OK, ESP_ERR_INVALID_STATE (not connected), ESP_FAIL on write.
 */
esp_err_t cloud_link_send_text(const char *chat_id, const char *text);

/**
 * @brief True when the WebSocket is currently connected.
 */
bool cloud_link_is_connected(void);
/**
 * @brief Send current on-device MEMORY.md and cron jobs report to backend (frame: device_sync_report).
 */
esp_err_t cloud_link_send_sync_report(void);

/**
 * @brief Relay one LLM request through the cloud gateway (frame:
 *        llm.request / llm.response). The cloud adds its own API key and
 *        calls the OpenAI-compatible provider; the raw provider response
 *        body is copied back to the caller.
 *
 * Blocks up to timeout_ms. Only one request may be in flight (the agent
 * loop is single-threaded). resp_buf should be MIMI_LLM_STREAM_BUF_SIZE.
 *
 * @param body       Request JSON string (OpenAI chat-completions format),
 *                   already built by llm_proxy.
 * @param resp_buf   Output buffer for the raw provider response body.
 * @param resp_cap   Size of resp_buf.
 * @param resp_len   Receives the response length.
 * @param status     Receives the provider HTTP status (0 on transport error).
 * @param timeout_ms Overall wait for the response frame.
 * @return ESP_OK (check *status), ESP_ERR_INVALID_STATE (not connected),
 *         ESP_ERR_TIMEOUT, ESP_FAIL.
 */
esp_err_t cloud_link_llm_request(const char *body, size_t body_len,
                                 char *resp_buf, size_t resp_cap,
                                 size_t *resp_len, int *status,
                                 uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
