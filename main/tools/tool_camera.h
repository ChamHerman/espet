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
 * @brief Start capturing a sequence of frames (Video Burst/Stream mock)
 */
void tool_camera_video_start(void);

/**
 * @brief Stop capturing frames
 */
void tool_camera_video_stop(void);
