#include "tools/tool_camera.h"
#include "mimi_config.h"
#include "sync/backend_discovery.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "tool_camera";
static SemaphoreHandle_t s_cam_mutex = NULL;
static volatile bool s_video_running = false;

// ESP32-S3 Camera Pins (as provided by user)
#define CAM_PIN_PWDN    -1 // Not connected
#define CAM_PIN_RESET   -1 // Not connected
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD    4
#define CAM_PIN_SIOC    5
#define CAM_PIN_D7      16 // Y9
#define CAM_PIN_D6      17 // Y8
#define CAM_PIN_D5      18 // Y7
#define CAM_PIN_D4      12 // Y6
#define CAM_PIN_D3      10 // Y5
#define CAM_PIN_D2      8  // Y4
#define CAM_PIN_D1      9  // Y3
#define CAM_PIN_D0      11 // Y2
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK    13

esp_err_t tool_camera_init(void)
{
    if (!s_cam_mutex) {
        s_cam_mutex = xSemaphoreCreateBinary();
        if (!s_cam_mutex) {
            return ESP_ERR_NO_MEM;
        }
        xSemaphoreGive(s_cam_mutex);
    }

    camera_config_t camera_config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sscb_sda = CAM_PIN_SIOD,
        .pin_sscb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        // XCLK 20MHz or 10MHz for OV2640
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        
        .pixel_format = PIXFORMAT_JPEG, // JPEG format
        .frame_size = FRAMESIZE_VGA,    // 640x480 (Good balance of size/quality)
        .jpeg_quality = 12,             // 0-63, lower is better quality
        .fb_count = 2,                  // Number of frame buffers
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST
    };

    // Initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed: %s", esp_err_to_name(err));
        return err;
    }

    // Optional: Vertical flip if camera is mounted upside down
    // sensor_t * s = esp_camera_sensor_get();
    // if (s != NULL) {
    //     s->set_vflip(s, 1);
    // }

    ESP_LOGI(TAG, "Camera Initialized Successfully");
    return ESP_OK;
}

camera_fb_t* tool_camera_take_picture(void)
{
    ESP_LOGI(TAG, "Taking picture (flushing stale frame)...");
    if (s_cam_mutex && xSemaphoreTake(s_cam_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Camera busy");
        return NULL;
    }

    /* With CAMERA_GRAB_LATEST and fb_count=2, discard the pre-buffered frame
     * so that the driver captures a brand new frame exposed right now. */
    camera_fb_t *stale_fb = esp_camera_fb_get();
    if (stale_fb) {
        esp_camera_fb_return(stale_fb);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        if (s_cam_mutex) {
            xSemaphoreGive(s_cam_mutex);
        }
        return NULL;
    }
    ESP_LOGI(TAG, "Picture taken! Size: %zu bytes (fresh frame)", fb->len);
    return fb;
}

void tool_camera_return_picture(camera_fb_t *fb)
{
    if (fb) {
        esp_camera_fb_return(fb);
    }
    if (s_cam_mutex) {
        xSemaphoreGive(s_cam_mutex);
    }
}

/* -----------------------------------------------------------------------
 * Live camera streaming: raw JPEG frames -> backend binary WebSocket.
 * --------------------------------------------------------------------- */

static volatile bool s_stream_running = false;
static int s_stream_fps = MIMI_CAM_STREAM_FPS_DEFAULT;
static TaskHandle_t s_stream_task = NULL;
static StackType_t *s_stream_stack = NULL;
static StaticTask_t *s_stream_tcb = NULL;

static void build_camera_ws_url(char *url, size_t url_sz)
{
    char base[96];
    char device_id[20];
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "ESP-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    backend_discovery_base_url(base, sizeof(base));
    if (strncmp(base, "http://", 7) == 0) {
        snprintf(url, url_sz, "ws://%s/api/camera/ws/device?key=%s&device_id=%s",
                 base + 7, MIMI_BACKEND_API_KEY, device_id);
    } else if (strncmp(base, "https://", 8) == 0) {
        snprintf(url, url_sz, "wss://%s/api/camera/ws/device?key=%s&device_id=%s",
                 base + 8, MIMI_BACKEND_API_KEY, device_id);
    } else {
        snprintf(url, url_sz, "ws://%s/api/camera/ws/device?key=%s&device_id=%s",
                 base, MIMI_BACKEND_API_KEY, device_id);
    }
}

static void camera_stream_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Camera stream task started");

    while (s_stream_running) {
        char url[192];
        build_camera_ws_url(url, sizeof(url));

        esp_websocket_client_config_t cfg = {
            .uri = url,
            .network_timeout_ms = 3000,
            .buffer_size = 4096,
            .task_stack = 4096,
            .disable_auto_reconnect = true,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        esp_websocket_client_handle_t ws = esp_websocket_client_init(&cfg);
        if (!ws || esp_websocket_client_start(ws) != ESP_OK) {
            ESP_LOGW(TAG, "Camera stream WS connect failed; retrying");
            if (ws) {
                esp_websocket_client_destroy(ws);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int wait_ms = 0;
        while (s_stream_running && !esp_websocket_client_is_connected(ws) && wait_ms < 3000) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_ms += 100;
        }
        if (!s_stream_running || !esp_websocket_client_is_connected(ws)) {
            ESP_LOGW(TAG, "Camera stream WS did not connect; retrying");
            esp_websocket_client_stop(ws);
            esp_websocket_client_destroy(ws);
            if (s_stream_running) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            continue;
        }

        ESP_LOGI(TAG, "Camera stream connected at %d fps", s_stream_fps);
        int consecutive_send_failures = 0;

        while (s_stream_running && esp_websocket_client_is_connected(ws)) {
            TickType_t start = xTaskGetTickCount();
            camera_fb_t *fb = NULL;
            if (!s_cam_mutex || xSemaphoreTake(s_cam_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                fb = esp_camera_fb_get();
                if (fb) {
                    int sent = esp_websocket_client_send_bin(ws, (const char *)fb->buf,
                                                             (int)fb->len, pdMS_TO_TICKS(200));
                    if (sent < 0) {
                        consecutive_send_failures++;
                        ESP_LOGW(TAG, "Camera stream frame send failed (%d); dropping frame",
                                 consecutive_send_failures);
                    } else {
                        consecutive_send_failures = 0;
                    }
                    esp_camera_fb_return(fb);
                }
                if (s_cam_mutex) {
                    xSemaphoreGive(s_cam_mutex);
                }
            }

            if (consecutive_send_failures >= 3) {
                ESP_LOGW(TAG, "Camera stream send stalled; reconnecting WS");
                break;
            }

            int fps = s_stream_fps;
            if (fps < 1) fps = 1;
            if (fps > MIMI_CAM_STREAM_FPS_MAX) fps = MIMI_CAM_STREAM_FPS_MAX;
            TickType_t frame_delay = pdMS_TO_TICKS(1000 / fps);
            TickType_t elapsed = xTaskGetTickCount() - start;
            if (elapsed < frame_delay) {
                vTaskDelay(frame_delay - elapsed);
            } else {
                vTaskDelay(1);
            }
        }

        esp_websocket_client_stop(ws);
        esp_websocket_client_destroy(ws);
        if (s_stream_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    ESP_LOGI(TAG, "Camera stream stopped");
    s_stream_running = false;
    s_stream_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t tool_camera_stream_start(int fps)
{
    if (fps <= 0) {
        fps = MIMI_CAM_STREAM_FPS_DEFAULT;
    }
    if (fps > MIMI_CAM_STREAM_FPS_MAX) {
        fps = MIMI_CAM_STREAM_FPS_MAX;
    }
    if (s_stream_running) {
        s_stream_fps = fps;
        return ESP_OK;
    }
    if (s_video_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_stream_task) {
        free(s_stream_stack); s_stream_stack = NULL;
        free(s_stream_tcb);   s_stream_tcb = NULL;
    }

    s_stream_fps = fps;
    s_stream_running = true;
    s_stream_stack = heap_caps_malloc(MIMI_CAM_STREAM_STACK * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    s_stream_tcb = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_stream_stack && s_stream_tcb) {
        s_stream_task = xTaskCreateStaticPinnedToCore(camera_stream_task, "cam_stream",
                                                      MIMI_CAM_STREAM_STACK, NULL, 5,
                                                      s_stream_stack, s_stream_tcb,
                                                      MIMI_CAM_STREAM_CORE);
    }
    if (!s_stream_task) {
        free(s_stream_stack); s_stream_stack = NULL;
        free(s_stream_tcb);   s_stream_tcb = NULL;
        if (xTaskCreatePinnedToCore(camera_stream_task, "cam_stream", 3072, NULL, 5,
                                    &s_stream_task, MIMI_CAM_STREAM_CORE) != pdPASS) {
            s_stream_running = false;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t tool_camera_stream_stop(void)
{
    s_stream_running = false;
    int64_t deadline = esp_timer_get_time() + 3000000;
    while (s_stream_task != NULL && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!s_stream_task) {
        free(s_stream_stack); s_stream_stack = NULL;
        free(s_stream_tcb);   s_stream_tcb = NULL;
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

bool tool_camera_stream_is_running(void)
{
    return s_stream_running;
}

/* -----------------------------------------------------------------------
 * Video recording: JPEG frames -> PSRAM -> in-place MJPEG AVI
 *
 * The OV2640 already outputs JPEG, so frames are stored verbatim inside a
 * minimal AVI container (MJPG fourcc) that Telegram's sendVideo accepts.
 * Layout of the store buffer while recording:
 *
 *   [AVI header reserve][00dc<len><jpeg>][00dc<len><jpeg>]...
 *
 * The header is written last (its length is constant, computed once at
 * start), the idx1 index is appended after the last chunk at stop, so the
 * whole file assembles in place with no copies.
 * --------------------------------------------------------------------- */

#define VID_FPS            10
#define VID_MAX_BYTES      (4u * 1024u * 1024u)  /* frame data cap (PSRAM) */
#define VID_MAX_FRAMES     300u                  /* ~30 s at VID_FPS */
#define VID_AVI_MAX_BYTES  (VID_MAX_BYTES + 16u * VID_MAX_FRAMES + 1024u)

static TaskHandle_t s_video_task = NULL;
static StackType_t *s_video_stack = NULL;     /* PSRAM-allocated */
static StaticTask_t *s_video_tcb = NULL;      /* internal RAM */
static uint8_t *s_vid_buf = NULL;             /* PSRAM AVI store */
static size_t s_vid_buf_len = 0;              /* header reserve + chunks so far */
static size_t s_vid_data_start = 0;           /* offset of first 00dc chunk */
static uint32_t s_vid_sizes[VID_MAX_FRAMES];
static uint32_t s_vid_frame_count = 0;
static uint32_t s_vid_max_frame = 0;
static int64_t s_vid_elapsed_us = 0;

static inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void put4cc(uint8_t *p, const char *fourcc)
{
    memcpy(p, fourcc, 4);
}

/* Write the constant-length AVI header (everything before the first 00dc
 * chunk). Returns the header length in bytes — the same value the frame
 * store reserved at recording start. */
static size_t avi_build_header(uint8_t *b, uint32_t n_frames,
                               uint32_t data_bytes, uint32_t max_frame,
                               uint32_t fps_rate, uint32_t fps_scale,
                               uint32_t width, uint32_t height)
{
    const uint32_t idx_size = 8 + 16 * n_frames;
    const uint32_t movi_list_size = 4 + data_bytes;  /* 'movi' fourcc + chunks */
    const uint32_t hdrl_list_size = 4 + 64 + 124;    /* 'hdrl' + avih + strl */
    const uint32_t riff_size =
        4 + (8 + hdrl_list_size) + (12 + movi_list_size) + idx_size;
    const uint32_t us_per_frame = fps_rate ? (uint32_t)(((uint64_t)fps_scale * 1000000) / fps_rate) : 100000;

    size_t o = 0;
    put4cc(b + o, "RIFF"); put32(b + o + 4, riff_size); put4cc(b + o + 8, "AVI "); o += 12;

    /* LIST hdrl */
    put4cc(b + o, "LIST"); put32(b + o + 4, hdrl_list_size); put4cc(b + o + 8, "hdrl"); o += 12;
    put4cc(b + o, "avih"); put32(b + o + 4, 56); o += 8;
    put32(b + o, us_per_frame);                         /* dwMicroSecPerFrame */
    put32(b + o + 4, us_per_frame * n_frames);          /* dwMaxBytesPerSec (rough) */
    put32(b + o + 8, 0);                                /* dwPaddingGranularity */
    put32(b + o + 12, 0x10);                            /* dwFlags: AVIF_HASINDEX */
    put32(b + o + 16, n_frames);                        /* dwTotalFrames */
    put32(b + o + 20, 0);                               /* dwInitialFrames */
    put32(b + o + 24, 1);                               /* dwStreams */
    put32(b + o + 28, max_frame + 8);                   /* dwSuggestedBufferSize */
    put32(b + o + 32, width);                           /* dwWidth */
    put32(b + o + 36, height);                          /* dwHeight */
    put32(b + o + 40, 0); put32(b + o + 44, 0);         /* dwReserved[4] */
    put32(b + o + 48, 0); put32(b + o + 52, 0);
    o += 56;

    /* LIST strl */
    put4cc(b + o, "LIST"); put32(b + o + 4, 116); put4cc(b + o + 8, "strl"); o += 12;
    put4cc(b + o, "strh"); put32(b + o + 4, 56); o += 8;
    put4cc(b + o, "vids");                              /* fccType */
    put4cc(b + o + 4, "MJPG");                          /* fccHandler */
    put32(b + o + 8, 0);                                /* dwFlags */
    put16(b + o + 12, 0); put16(b + o + 14, 0);         /* wPriority, wLanguage */
    put32(b + o + 16, 0);                               /* dwInitialFrames */
    put32(b + o + 20, fps_scale);                       /* dwScale */
    put32(b + o + 24, fps_rate);                        /* dwRate */
    put32(b + o + 28, 0);                               /* dwStart */
    put32(b + o + 32, n_frames);                        /* dwLength */
    put32(b + o + 36, max_frame + 8);                   /* dwSuggestedBufferSize */
    put32(b + o + 40, 10000);                           /* dwQuality */
    put32(b + o + 44, 0);                               /* dwSampleSize */
    put16(b + o + 48, 0); put16(b + o + 50, 0);         /* rcFrame */
    put16(b + o + 52, (uint16_t)width); put16(b + o + 54, (uint16_t)height);
    o += 56;
    put4cc(b + o, "strf"); put32(b + o + 4, 40); o += 8; /* BITMAPINFOHEADER */
    put32(b + o, 40);
    put32(b + o + 4, width);
    put32(b + o + 8, height);
    put16(b + o + 12, 1);                               /* biPlanes */
    put16(b + o + 14, 24);                              /* biBitCount */
    put4cc(b + o + 16, "MJPG");                         /* biCompression */
    put32(b + o + 20, width * height * 3);              /* biSizeImage */
    put32(b + o + 24, 0); put32(b + o + 28, 0);         /* biX/YPelsPerMeter */
    put32(b + o + 32, 0); put32(b + o + 36, 0);         /* biClrUsed/Important */
    o += 40;

    /* LIST movi (chunks follow immediately) */
    put4cc(b + o, "LIST"); put32(b + o + 4, movi_list_size); put4cc(b + o + 8, "movi"); o += 12;

    return o;
}

static void video_task(void *arg)
{
    ESP_LOGI(TAG, "Video recording started (cap %u KB / %u frames)",
             (unsigned)(VID_MAX_BYTES / 1024), (unsigned)VID_MAX_FRAMES);

    int64_t start_us = esp_timer_get_time();

    while (s_video_running) {
        int64_t frame_start_us = esp_timer_get_time();

        camera_fb_t *fb = NULL;
        if (s_cam_mutex && xSemaphoreTake(s_cam_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(TAG, "Camera busy during video frame");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Failed to capture video frame");
            if (s_cam_mutex) {
                xSemaphoreGive(s_cam_mutex);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (s_vid_frame_count >= VID_MAX_FRAMES ||
            s_vid_buf_len + 8 + fb->len > VID_MAX_BYTES + s_vid_data_start) {
            ESP_LOGW(TAG, "Video cap reached (%u frames, %u KB) — auto-stopping",
                     (unsigned)s_vid_frame_count,
                     (unsigned)((s_vid_buf_len - s_vid_data_start) / 1024));
            s_video_running = false;
        } else {
            uint8_t *p = s_vid_buf + s_vid_buf_len;
            put4cc(p, "00dc");
            put32(p + 4, (uint32_t)fb->len);
            memcpy(p + 8, fb->buf, fb->len);
            s_vid_buf_len += 8 + fb->len;
            s_vid_sizes[s_vid_frame_count] = (uint32_t)fb->len;
            s_vid_frame_count++;
            if (fb->len > s_vid_max_frame) {
                s_vid_max_frame = (uint32_t)fb->len;
            }
        }

        esp_camera_fb_return(fb);
        if (s_cam_mutex) {
            xSemaphoreGive(s_cam_mutex);
        }

        if (!s_video_running) {
            break;
        }
        /* Pace to VID_FPS based on how long the capture itself took */
        int64_t elapsed_us = esp_timer_get_time() - frame_start_us;
        int64_t remain_us = 1000000 / VID_FPS - elapsed_us;
        if (remain_us > 0) {
            vTaskDelay(pdMS_TO_TICKS(remain_us / 1000));
        }
    }

    s_vid_elapsed_us = esp_timer_get_time() - start_us;
    ESP_LOGI(TAG, "Video capture task exiting: %u frames, %u KB",
             (unsigned)s_vid_frame_count,
             (unsigned)((s_vid_buf_len - s_vid_data_start) / 1024));
    s_video_task = NULL;
    vTaskDelete(NULL);
}

void tool_camera_video_start(void)
{
    if (s_video_running || s_vid_buf) {
        ESP_LOGW(TAG, "Cannot start video: previous capture still running or not released");
        return;
    }
    if (s_stream_running) {
        ESP_LOGW(TAG, "Stopping live stream before video capture");
        tool_camera_stream_stop();
    }

    s_vid_buf = heap_caps_calloc(1, VID_AVI_MAX_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_vid_buf) {
        ESP_LOGE(TAG, "Video buffer alloc failed (%u KB PSRAM)",
                 (unsigned)(VID_AVI_MAX_BYTES / 1024));
        return;
    }

    /* Header length is constant — compute it once with zeroed counters and
     * reserve exactly that much at the front of the store. */
    uint8_t scratch[512];
    s_vid_data_start = avi_build_header(scratch, 0, 0, 0, VID_FPS, 1,
                                        640, 480);
    s_vid_buf_len = s_vid_data_start;
    s_vid_frame_count = 0;
    s_vid_max_frame = 0;
    s_vid_elapsed_us = 0;
    s_video_running = true;

    /* The internal heap is too fragmented after WiFi/TLS/telegram come up to
     * hand out a contiguous task stack (field failure — same reason the mDNS
     * task fails to start), so the stack lives in PSRAM via xTaskCreateStatic
     * (needs CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y). */
    s_video_stack = heap_caps_malloc(4096 * sizeof(StackType_t),
                                     MALLOC_CAP_SPIRAM);
    s_video_tcb = heap_caps_malloc(sizeof(StaticTask_t),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_video_stack && s_video_tcb) {
        s_video_task = xTaskCreateStatic(video_task, "video_task", 4096, NULL,
                                         5, s_video_stack, s_video_tcb);
    }
    if (!s_video_task) {
        /* Fallback: classic dynamic creation with a small internal stack */
        free(s_video_stack); s_video_stack = NULL;
        free(s_video_tcb);   s_video_tcb = NULL;
        if (xTaskCreate(video_task, "video_task", 3072, NULL, 5,
                        &s_video_task) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create video task (static + dynamic)");
            s_video_running = false;
            free(s_vid_buf);
            s_vid_buf = NULL;
            return;
        }
        ESP_LOGW(TAG, "video task using small internal-stack fallback");
    }
}

esp_err_t tool_camera_video_stop_and_get(uint8_t **avi_out, size_t *avi_len)
{
    if (avi_out) *avi_out = NULL;
    if (avi_len) *avi_len = 0;

    if (!s_vid_buf) {
        return ESP_ERR_INVALID_STATE;
    }

    s_video_running = false;
    int64_t deadline = esp_timer_get_time() + 3000000;
    while (s_video_task != NULL && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_video_task != NULL) {
        ESP_LOGE(TAG, "Video task did not exit in time");
        return ESP_ERR_TIMEOUT;
    }

    if (s_vid_frame_count == 0) {
        ESP_LOGW(TAG, "No frames captured — nothing to send");
        tool_camera_video_release();
        return ESP_ERR_NOT_FOUND;
    }

    /* Real measured fps: rate/scale = frame_count / elapsed_seconds */
    uint32_t us_per_frame = (uint32_t)(s_vid_elapsed_us / s_vid_frame_count);
    if (us_per_frame == 0) {
        us_per_frame = 1000000 / VID_FPS;
    }

    uint32_t data_bytes = (uint32_t)(s_vid_buf_len - s_vid_data_start);
    size_t hdr_len = avi_build_header(s_vid_buf, s_vid_frame_count, data_bytes,
                                      s_vid_max_frame, 1000000, us_per_frame,
                                      640, 480);
    if (hdr_len != s_vid_data_start) {
        /* Should never happen — the header length is constant by design */
        ESP_LOGE(TAG, "Header length changed (%u != %u)", (unsigned)hdr_len,
                 (unsigned)s_vid_data_start);
        tool_camera_video_release();
        return ESP_FAIL;
    }

    /* Append idx1 after the last chunk. Entry offsets are relative to the
     * 'movi' fourcc; the first chunk sits 4 bytes after it. */
    uint8_t *idx = s_vid_buf + s_vid_buf_len;
    put4cc(idx, "idx1");
    put32(idx + 4, 16 * s_vid_frame_count);
    uint32_t chunk_rel = 4;
    for (uint32_t i = 0; i < s_vid_frame_count; i++) {
        uint8_t *e = idx + 8 + i * 16;
        put4cc(e, "00dc");
        put32(e + 4, 0x10);                    /* AVIIF_KEYFRAME */
        put32(e + 8, chunk_rel);
        put32(e + 12, s_vid_sizes[i]);
        chunk_rel += 8 + s_vid_sizes[i];
    }

    *avi_out = s_vid_buf;
    *avi_len = s_vid_buf_len + 8 + 16 * s_vid_frame_count;
    ESP_LOGI(TAG, "AVI assembled: %u bytes, %u frames, %u ms",
             (unsigned)*avi_len, (unsigned)s_vid_frame_count,
             (unsigned)(s_vid_elapsed_us / 1000));
    /* The store stays owned by tool_camera — the caller reads *avi_out and
     * then calls tool_camera_video_release() to actually free the PSRAM. */
    return ESP_OK;
}

void tool_camera_video_release(void)
{
    if (s_vid_buf) {
        free(s_vid_buf);
        s_vid_buf = NULL;
    }
    s_vid_buf_len = 0;
    s_vid_frame_count = 0;
}
