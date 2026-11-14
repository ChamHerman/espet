/*
 * display_face.h — Expressive animated eye system for ESPET OLED display.
 *
 * Inspired by intellar/oled_eye_display, procedural rendering of stylized
 * eyes with natural blinking, saccades, and cognitive state reactions.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ssd1306.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACE_STATE_IDLE = 0,    /* Natural breathing, blinking, micro-saccades */
    FACE_STATE_LISTEN,      /* Eyes wide, attentive, looking forward */
    FACE_STATE_THINK,       /* Eyes glancing up / pondering */
    FACE_STATE_SPEAK,       /* Expressive talking / bouncing eyes */
    FACE_STATE_HAPPY,       /* Cheerful smiling eyelid arches */
    FACE_STATE_SLEEP,       /* Resting horizontal slits */
    FACE_STATE_LOOK_LEFT,   /* Looking towards the left */
    FACE_STATE_LOOK_RIGHT,  /* Looking towards the right */
    FACE_STATE_CONFUSED,    /* Asymmetric quizzical eyes */
    FACE_STATE_TEXT,        /* Text mode for setup, BLE, or diagnostics */
} face_state_t;

/* Initialize face animation engine with SSD1306 handle and mutex. */
void display_face_init(ssd1306_handle_t dev, SemaphoreHandle_t mutex);

/* Transition to a new face state. Thread-safe. */
void display_face_set_state(face_state_t state);

/* Query the current face state. */
face_state_t display_face_get_state(void);

/* Trigger immediate one-shot action. */
void display_face_trigger_blink(void);
void display_face_trigger_happy(void);

/* Render a single face frame (called internally by oled_anim_task). */
void display_face_render_frame(void);

#ifdef __cplusplus
}
#endif
