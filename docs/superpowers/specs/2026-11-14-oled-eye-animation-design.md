# Design Document: OLED Expressive Eye Animations for ESPET

## 1. Overview
This design integrates the procedural animated eye system inspired by [`intellar/oled_eye_display`](https://github.com/intellar/oled_eye_display) into ESPET's SSD1306 128×64 OLED display.

The eyes serve as ESPET's living face, replacing the static `"Say Hey ESPET"` text during idle and morphing dynamically to reflect the embodied assistant's cognitive and communicative states (Listening, Thinking, Speaking, Happy, Sleeping).

## 2. Architecture & Data Flow

```
[Agent Loop / Voice Client / Mimi Main]
                 │
                 ▼
  display_face_set_state(state)   OR   display_status_set(line1, line2)
                 │
                 ▼
        [s_face_state mutex / queue]
                 │
                 ▼
    [FreeRTOS Task: oled_anim_task (Priority 5, 4 KB Stack)]
                 │
                 ├─ 1. Interpolate coordinates (x, y, width, height, corner_radius)
                 ├─ 2. Draw filled rounded rectangles (left_eye, right_eye)
                 ├─ 3. Draw eyelid cutouts (for happy / squint expressions)
                 └─ 4. Flush to SSD1306 GRAM via I2C (GPIO 2 SCL, GPIO 1 SDA)
```

## 3. Eye Geometry & Animation Parameters

Screen dimensions: **128 × 64 pixels**.
Reference Eye parameters:
- `REF_EYE_WIDTH = 40`
- `REF_EYE_HEIGHT = 40`
- `REF_SPACE_BETWEEN_EYES = 10`
- `REF_CORNER_RADIUS = 10`
- Left eye center: `(X = 39, Y = 32)`
- Right eye center: `(X = 89, Y = 32)`

### States & Expressions
1. **`FACE_STATE_IDLE`**:
   - Default resting face.
   - Natural randomized blinking every 3–5 seconds (duration ~200 ms).
   - Natural micro-saccades (eyes glance left/right by ±8 px, pause, and return).
2. **`FACE_STATE_LISTEN`**:
   - Eyes widen (`height = 46`, `width = 42`), centered, attentive.
3. **`FACE_STATE_THINK`**:
   - Eyes shift to upper right/left corner (`X += 8`, `Y -= 6`), subtle oscillation.
4. **`FACE_STATE_SPEAK`**:
   - Animated cheerful bounce or happy eye arches (`happy_eye()`).
5. **`FACE_STATE_HAPPY`**:
   - Inverted lower eyelid cutouts creating smiling anime-style arches.
6. **`FACE_STATE_SLEEP`**:
   - Eyes closed to 2 px horizontal slits (`height = 2`, `radius = 0`).
7. **`FACE_STATE_TEXT`**:
   - Fallback text mode for Wi-Fi onboarding (`PROV_ESPET`), boot initialization, or critical diagnostic errors.

## 4. Hardware Safety & Performance
- Uses existing I2C driver on `I2C_NUM_0` (`SCL = GPIO 2`, `SDA = GPIO 1`).
- Frame rate: ~25–30 FPS with non-blocking delays using FreeRTOS `vTaskDelay`.
- Thread-safe access to SSD1306 device handle via mutex (`s_lock`).

