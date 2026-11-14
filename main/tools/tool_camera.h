#pragma once

#include "esp_err.h"
#include "esp_camera.h"
#include <stdbool.h>

/**
 * @brief Initialize the camera with the specific ESP32-S3 pins
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t tool_camera_init(void);

/**
 * @brief Take a picture and return the framebuffer
 * @return Pointer to camera_fb_t, or NULL if failed. 
 *         Remember to call esp_camera_fb_return(fb) when done!
 */
camera_fb_t* tool_camera_take_picture(void);

/**
 * @brief Free the framebuffer returned by tool_camera_take_picture
 */
void tool_camera_return_picture(camera_fb_t *fb);

/**
 * @brief Start recording video: JPEG frames are captured into a PSRAM buffer
 *        at ~10 fps until tool_camera_video_stop_and_get() is called or the
 *        buffer/frame cap is hit (auto-stop).
 */
void tool_camera_video_start(void);

/**
 * @brief Stop recording, finalize the frame store into an in-place MJPEG AVI
 *        buffer, and hand it to the caller for upload.
 *
 * Blocks until the capture task exits (max 3 s). On success the caller owns
 * the buffer returned via *avi_out and MUST free it with
 * tool_camera_video_release().
 *
 * @param avi_out Receives the AVI buffer (NULL if no frames were captured).
 * @param avi_len Receives the AVI size in bytes.
 * @return ESP_OK, ESP_ERR_INVALID_STATE (no recording active),
 *         ESP_ERR_NOT_FOUND (zero frames), ESP_ERR_TIMEOUT.
 */
esp_err_t tool_camera_video_stop_and_get(uint8_t **avi_out, size_t *avi_len);

/**
 * @brief Free the AVI buffer returned by tool_camera_video_stop_and_get.
 */
void tool_camera_video_release(void);

/**
 * @brief Start/stop live JPEG streaming to the FastAPI backend camera WebSocket.
 *        Streaming is controlled by backend viewer reference-counts and shares
 *        the camera mutex with still photos and video recording.
 */
esp_err_t tool_camera_stream_start(int fps);
esp_err_t tool_camera_stream_stop(void);
bool tool_camera_stream_is_running(void);
