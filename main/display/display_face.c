/*
 * display_face.c — Expressive animated eye system for ESPET OLED display.
 *
 * Implements procedural geometry (rounded rectangles, saccades, blinks,
 * and emotional eyelid shapes) inspired by intellar/oled_eye_display.
 */
#include "display_face.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ssd1306.h"

static const char *TAG = "face";

#define SCREEN_W 128
#define SCREEN_H 64

#define REF_EYE_W 40
#define REF_EYE_H 40
#define REF_CORNER_RADIUS 10
#define REF_EYE_GAP 10

/* Centered coordinates:
 * Left eye center: (128 / 2 - 40 / 2 - 10 / 2) = 39
 * Right eye center: (128 / 2 + 40 / 2 + 10 / 2) = 89
 * Y center: 32 */
#define DEFAULT_LEFT_X  39
#define DEFAULT_RIGHT_X 89
#define DEFAULT_CENTER_Y 32

typedef struct {
    int x;
    int y;
    int width;
    int height;
    int radius;
} eye_state_t;

static ssd1306_handle_t s_dev = NULL;
static SemaphoreHandle_t s_display_mutex = NULL;
static TaskHandle_t s_anim_task_handle = NULL;
static volatile face_state_t s_face_state = FACE_STATE_IDLE;
static eye_state_t s_left_eye;
static eye_state_t s_right_eye;

/* Blink and saccade animation control */
static volatile bool s_blink_active = false;
static int s_blink_stage = 0;
static int64_t s_next_blink_time_us = 0;
static int64_t s_next_saccade_time_us = 0;
static int s_saccade_offset_x = 0;
static int s_saccade_offset_y = 0;
static int s_saccade_hold_frames = 0;

/* Triangle coordinates for happy eye cuts */
static int s_happy_offset = 0;
static bool s_happy_active = false;
static int s_happy_hold_frames = 0;

/* Helpers for safe radius calculation */
static int calculate_safe_radius(int r, int w, int h)
{
    if (w < 2 * (r + 1)) {
        r = (w / 2) - 1;
    }
    if (h < 2 * (r + 1)) {
        r = (h / 2) - 1;
    }
    return (r < 0) ? 0 : r;
}

/* Fast Bresenham/Scanline filled rounded rectangle */
static void draw_filled_round_rect(ssd1306_handle_t dev, int cx, int cy, int w, int h, int r, uint8_t color)
{
    if (!dev || w <= 0 || h <= 0) return;
    r = calculate_safe_radius(r, w, h);

    int x0 = cx - (w / 2);
    int y0 = cy - (h / 2);
    int r2 = r * r;

    for (int dy = 0; dy < h; dy++) {
        int py = y0 + dy;
        if (py < 0 || py >= SCREEN_H) continue;

        int x_left = x0;
        int x_right = x0 + w - 1;

        if (r > 0) {
            if (dy < r) {
                int dist_y = r - dy;
                int dx = 0;
                while ((dx + 1) * (dx + 1) + dist_y * dist_y <= r2) {
                    dx++;
                }
                x_left = x0 + r - dx;
                x_right = x0 + w - 1 - r + dx;
            } else if (dy >= h - r) {
                int dist_y = dy - (h - r - 1);
                int dx = 0;
                while ((dx + 1) * (dx + 1) + dist_y * dist_y <= r2) {
                    dx++;
                }
                x_left = x0 + r - dx;
                x_right = x0 + w - 1 - r + dx;
            }
        }

        if (x_left < 0) x_left = 0;
        if (x_right >= SCREEN_W) x_right = SCREEN_W - 1;

        for (int px = x_left; px <= x_right; px++) {
            ssd1306_fill_point(dev, (uint8_t)px, (uint8_t)py, color);
        }
    }
}

/* Scanline filled triangle for expressive cutouts (e.g. happy eye arches) */
static void draw_filled_triangle(ssd1306_handle_t dev, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color)
{
    if (!dev) return;

    /* Sort vertices by Y: y0 <= y1 <= y2 */
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

    int total_height = y2 - y0;
    if (total_height == 0) return;

    for (int i = 0; i < total_height; i++) {
        int py = y0 + i;
        if (py < 0 || py >= SCREEN_H) continue;

        bool second_half = i > (y1 - y0) || y1 == y0;
        int segment_height = second_half ? (y2 - y1) : (y1 - y0);
        if (segment_height == 0) continue;

        float alpha = (float)i / total_height;
        float beta = (float)(i - (second_half ? (y1 - y0) : 0)) / segment_height;

        int ax = x0 + (int)((x2 - x0) * alpha);
        int bx = second_half ? (x1 + (int)((x2 - x1) * beta)) : (x0 + (int)((x1 - x0) * beta));

        if (ax > bx) { int t = ax; ax = bx; bx = t; }
        if (ax < 0) ax = 0;
        if (bx >= SCREEN_W) bx = SCREEN_W - 1;

        for (int px = ax; px <= bx; px++) {
            ssd1306_fill_point(dev, (uint8_t)px, (uint8_t)py, color);
        }
    }
}

static void schedule_next_blink(void)
{
    /* Natural blink interval: 2.5 to 5.5 seconds */
    uint32_t delay_ms = 2500 + (esp_random() % 3000);
    s_next_blink_time_us = esp_timer_get_time() + ((int64_t)delay_ms * 1000);
}

static void schedule_next_saccade(void)
{
    /* Natural eye movement: every 3.5 to 7.0 seconds */
    uint32_t delay_ms = 3500 + (esp_random() % 3500);
    s_next_saccade_time_us = esp_timer_get_time() + ((int64_t)delay_ms * 1000);
}

static void reset_eyes_to_state(face_state_t state)
{
    switch (state) {
        case FACE_STATE_LISTEN:
            /* Wide attentive eyes */
            s_left_eye.width = REF_EYE_W + 2;
            s_left_eye.height = REF_EYE_H + 4;
            s_left_eye.x = DEFAULT_LEFT_X;
            s_left_eye.y = DEFAULT_CENTER_Y;
            s_left_eye.radius = REF_CORNER_RADIUS + 2;

            s_right_eye.width = REF_EYE_W + 2;
            s_right_eye.height = REF_EYE_H + 4;
            s_right_eye.x = DEFAULT_RIGHT_X;
            s_right_eye.y = DEFAULT_CENTER_Y;
            s_right_eye.radius = REF_CORNER_RADIUS + 2;
            break;

        case FACE_STATE_THINK:
            /* Gaze up and to the right */
            s_left_eye.width = REF_EYE_W - 4;
            s_left_eye.height = REF_EYE_H - 4;
            s_left_eye.x = DEFAULT_LEFT_X + 6;
            s_left_eye.y = DEFAULT_CENTER_Y - 6;
            s_left_eye.radius = REF_CORNER_RADIUS - 2;

            s_right_eye.width = REF_EYE_W - 4;
            s_right_eye.height = REF_EYE_H - 4;
            s_right_eye.x = DEFAULT_RIGHT_X + 6;
            s_right_eye.y = DEFAULT_CENTER_Y - 6;
            s_right_eye.radius = REF_CORNER_RADIUS - 2;
            break;

        case FACE_STATE_SPEAK:
            /* Lively eyes */
            s_left_eye.width = REF_EYE_W;
            s_left_eye.height = REF_EYE_H;
            s_left_eye.x = DEFAULT_LEFT_X;
            s_left_eye.y = DEFAULT_CENTER_Y;
            s_left_eye.radius = REF_CORNER_RADIUS;

            s_right_eye.width = REF_EYE_W;
            s_right_eye.height = REF_EYE_H;
            s_right_eye.x = DEFAULT_RIGHT_X;
            s_right_eye.y = DEFAULT_CENTER_Y;
            s_right_eye.radius = REF_CORNER_RADIUS;
            break;

        case FACE_STATE_HAPPY:
            s_left_eye.width = REF_EYE_W;
            s_left_eye.height = REF_EYE_H;
            s_left_eye.x = DEFAULT_LEFT_X;
            s_left_eye.y = DEFAULT_CENTER_Y;
            s_left_eye.radius = REF_CORNER_RADIUS;

            s_right_eye.width = REF_EYE_W;
            s_right_eye.height = REF_EYE_H;
            s_right_eye.x = DEFAULT_RIGHT_X;
            s_right_eye.y = DEFAULT_CENTER_Y;
            s_right_eye.radius = REF_CORNER_RADIUS;
            break;

        case FACE_STATE_SLEEP:
            /* Resting 2px slits */
            s_left_eye.width = REF_EYE_W;
            s_left_eye.height = 2;
            s_left_eye.x = DEFAULT_LEFT_X;
            s_left_eye.y = DEFAULT_CENTER_Y;
            s_left_eye.radius = 0;

            s_right_eye.width = REF_EYE_W;
            s_right_eye.height = 2;
            s_right_eye.x = DEFAULT_RIGHT_X;
            s_right_eye.y = DEFAULT_CENTER_Y;
            s_right_eye.radius = 0;
            break;

        case FACE_STATE_LOOK_LEFT:
            s_left_eye.width = REF_EYE_W;
            s_left_eye.height = REF_EYE_H;
            s_left_eye.x = DEFAULT_LEFT_X - 8;
            s_left_eye.y = DEFAULT_CENTER_Y;
            s_left_eye.radius = REF_CORNER_RADIUS;

            s_right_eye.width = REF_EYE_W;
            s_right_eye.height = REF_EYE_H;
            s_right_eye.x = DEFAULT_RIGHT_X - 8;
            s_right_eye.y = DEFAULT_CENTER_Y;
            s_right_eye.radius = REF_CORNER_RADIUS;
            break;

        case FACE_STATE_LOOK_RIGHT:
            s_left_eye.width = REF_EYE_W;
            s_left_eye.height = REF_EYE_H;
            s_left_eye.x = DEFAULT_LEFT_X + 8;
            s_left_eye.y = DEFAULT_CENTER_Y;
            s_left_eye.radius = REF_CORNER_RADIUS;

            s_right_eye.width = REF_EYE_W;
            s_right_eye.height = REF_EYE_H;
            s_right_eye.x = DEFAULT_RIGHT_X + 8;
            s_right_eye.y = DEFAULT_CENTER_Y;
            s_right_eye.radius = REF_CORNER_RADIUS;
            break;

        case FACE_STATE_CONFUSED:
            s_left_eye.width = REF_EYE_W;
            s_left_eye.height = REF_EYE_H + 2;
            s_left_eye.x = DEFAULT_LEFT_X;
            s_left_eye.y = DEFAULT_CENTER_Y - 2;
            s_left_eye.radius = REF_CORNER_RADIUS;

            s_right_eye.width = REF_EYE_W - 4;
            s_right_eye.height = 24;
            s_right_eye.x = DEFAULT_RIGHT_X;
            s_right_eye.y = DEFAULT_CENTER_Y + 4;
            s_right_eye.radius = 4;
            break;

        case FACE_STATE_IDLE:
        default:
            s_left_eye.width = REF_EYE_W;
            s_left_eye.height = REF_EYE_H;
            s_left_eye.x = DEFAULT_LEFT_X;
            s_left_eye.y = DEFAULT_CENTER_Y;
            s_left_eye.radius = REF_CORNER_RADIUS;

            s_right_eye.width = REF_EYE_W;
            s_right_eye.height = REF_EYE_H;
            s_right_eye.x = DEFAULT_RIGHT_X;
            s_right_eye.y = DEFAULT_CENTER_Y;
            s_right_eye.radius = REF_CORNER_RADIUS;
            break;
    }
}

/* Update animations (blinking, saccades) frame-by-frame */
static void update_animation_frame(void)
{
    int64_t now_us = esp_timer_get_time();

    /* 1. Blink handler */
    if (!s_blink_active && s_face_state != FACE_STATE_SLEEP && s_face_state != FACE_STATE_TEXT) {
        if (s_next_blink_time_us == 0) {
            schedule_next_blink();
        } else if (now_us >= s_next_blink_time_us) {
            s_blink_active = true;
            s_blink_stage = 0;
            schedule_next_blink();
        }
    }

    if (s_blink_active) {
        /* 6-step blink sequence: 3 frames down, 3 frames up */
        int speed = 12;
        if (s_blink_stage < 3) {
            s_left_eye.height -= speed;
            s_right_eye.height -= speed;
            s_left_eye.width += 2;
            s_right_eye.width += 2;
        } else if (s_blink_stage < 6) {
            s_left_eye.height += speed;
            s_right_eye.height += speed;
            s_left_eye.width -= 2;
            s_right_eye.width -= 2;
        }

        if (s_left_eye.height < 4) s_left_eye.height = 4;
        if (s_right_eye.height < 4) s_right_eye.height = 4;

        s_left_eye.radius = calculate_safe_radius(REF_CORNER_RADIUS, s_left_eye.width, s_left_eye.height);
        s_right_eye.radius = calculate_safe_radius(REF_CORNER_RADIUS, s_right_eye.width, s_right_eye.height);

        s_blink_stage++;
        if (s_blink_stage >= 6) {
            s_blink_active = false;
            reset_eyes_to_state(s_face_state);
        }
    }

    /* 2. Saccade (idle look around) handler */
    if (s_face_state == FACE_STATE_IDLE && !s_blink_active) {
        if (s_saccade_hold_frames > 0) {
            s_saccade_hold_frames--;
            if (s_saccade_hold_frames == 0) {
                /* Return to center */
                s_saccade_offset_x = 0;
                s_saccade_offset_y = 0;
                schedule_next_saccade();
            }
        } else {
            if (s_next_saccade_time_us == 0) {
                schedule_next_saccade();
            } else if (now_us >= s_next_saccade_time_us) {
                /* Pick random gaze: -1 (left), 0 (center), +1 (right) */
                int dir = (int)(esp_random() % 3) - 1;
                s_saccade_offset_x = dir * 8;
                s_saccade_offset_y = (esp_random() % 2 == 0) ? -2 : 0;
                s_saccade_hold_frames = 20 + (esp_random() % 30); /* Hold gaze ~0.6s to 1.5s */
                s_next_saccade_time_us = 0;
            }
        }
    }

    /* 3. Speaking subtle bounce */
    if (s_face_state == FACE_STATE_SPEAK) {
        static int s_speak_counter = 0;
        s_speak_counter++;
        s_saccade_offset_y = (s_speak_counter % 8 < 4) ? -2 : 0;
    }

    /* 4. Happy expression handler */
    if (s_face_state == FACE_STATE_HAPPY || s_happy_active) {
        if (!s_happy_active) {
            s_happy_active = true;
            s_happy_offset = REF_EYE_H / 2;
            s_happy_hold_frames = 45; /* Hold happy face ~1.5s */
        }
    }
    if (s_happy_active) {
        if (s_happy_hold_frames > 0) {
            s_happy_hold_frames--;
        } else {
            s_happy_active = false;
            if (s_face_state == FACE_STATE_HAPPY) {
                s_face_state = FACE_STATE_IDLE;
            }
            reset_eyes_to_state(s_face_state);
        }
    }
}

void display_face_render_frame(void)
{
    if (!s_dev || s_face_state == FACE_STATE_TEXT) return;

    update_animation_frame();

    /* Clear display */
    ssd1306_clear_screen(s_dev, 0x00);

    /* Draw left and right eyes */
    int lx = s_left_eye.x + s_saccade_offset_x;
    int ly = s_left_eye.y + s_saccade_offset_y;
    draw_filled_round_rect(s_dev, lx, ly, s_left_eye.width, s_left_eye.height, s_left_eye.radius, 1);

    int rx = s_right_eye.x + s_saccade_offset_x;
    int ry = s_right_eye.y + s_saccade_offset_y;
    draw_filled_round_rect(s_dev, rx, ry, s_right_eye.width, s_right_eye.height, s_right_eye.radius, 1);

    /* If happy expression active, cut out smiling eyelid triangles */
    if (s_happy_active) {
        int half_w = s_left_eye.width / 2;
        int half_h = s_left_eye.height / 2;

        /* Left eye smile cut */
        draw_filled_triangle(s_dev,
            lx - half_w - 2, ly + half_h + 2,
            lx, ly + 2,
            lx + half_w + 2, ly + half_h + 2,
            0 /* Black cutout */
        );

        /* Right eye smile cut */
        draw_filled_triangle(s_dev,
            rx - half_w - 2, ry + half_h + 2,
            rx, ry + 2,
            rx + half_w + 2, ry + half_h + 2,
            0 /* Black cutout */
        );
    }

    ssd1306_refresh_gram(s_dev);
}

static void oled_anim_task(void *pvParameters)
{
    ESP_LOGI(TAG, "oled_anim_task started (Priority 5, 4 KB Stack)");

    reset_eyes_to_state(s_face_state);
    schedule_next_blink();
    schedule_next_saccade();

    while (1) {
        if (s_face_state == FACE_STATE_TEXT) {
            /* In text mode, sleep longer to avoid wasting CPU */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (s_display_mutex) {
            if (xSemaphoreTake(s_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                display_face_render_frame();
                xSemaphoreGive(s_display_mutex);
            }
        } else {
            display_face_render_frame();
        }

        /* Target ~30 FPS (33 ms per frame) */
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

void display_face_init(ssd1306_handle_t dev, SemaphoreHandle_t mutex)
{
    s_dev = dev;
    s_display_mutex = mutex;
    reset_eyes_to_state(FACE_STATE_IDLE);

    if (s_anim_task_handle == NULL) {
        xTaskCreate(oled_anim_task, "oled_anim", 4096, NULL, 5, &s_anim_task_handle);
    }
}

void display_face_set_state(face_state_t state)
{
    if (s_face_state == state) return;

    ESP_LOGI(TAG, "Face state transition: %d -> %d", (int)s_face_state, (int)state);
    s_face_state = state;
    s_saccade_offset_x = 0;
    s_saccade_offset_y = 0;
    s_blink_active = false;

    if (state == FACE_STATE_HAPPY) {
        s_happy_active = true;
        s_happy_hold_frames = 45;
    }

    reset_eyes_to_state(state);
}

face_state_t display_face_get_state(void)
{
    return s_face_state;
}

void display_face_trigger_blink(void)
{
    s_blink_active = true;
    s_blink_stage = 0;
}

void display_face_trigger_happy(void)
{
    s_happy_active = true;
    s_happy_hold_frames = 45;
}
