# Implementation Plan: OLED Expressive Eye Animations for ESPET

## Phase 1: Header Definitions & Graphic Primitives
- Create `main/display/display_face.h`:
  - Declare `face_state_t` enum.
  - Declare `display_face_init(void)`.
  - Declare `display_face_set_state(face_state_t state)`.
  - Declare `display_face_get_state(void)`.
  - Declare `display_face_trigger_blink(void)`.
  - Declare `display_face_trigger_happy(void)`.

## Phase 2: Procedural Eye Engine (`main/display/display_face.c`)
- Implement:
  - `draw_filled_round_rect`: Scanline integer circle boundary for perfect corners on SSD1306.
  - `draw_filled_triangle`: Scanline rasterizer for eyelid cuts.
  - State structs `eye_state_t` with target and current positions.
  - Animation routines: `blink()`, `saccade()`, `wakeup()`, `sleep()`, `happy()`.
  - FreeRTOS task `oled_anim_task`:
    - Priority 5, 4096-byte stack, unpinned.
    - Handles state transitions, natural idle blinking, and saccades.

## Phase 3: Integration with `display_status.c` & `display_status.h`
- Update `display_status.c`:
  - Hook `display_status_init()` to launch face engine once hardware probes.
  - Map status strings in `display_status_set()` to appropriate `face_state_t`:
    - "Listening..." -> `FACE_STATE_LISTEN`
    - "Thinking..." / "Transcribing..." -> `FACE_STATE_THINK`
    - "Replying..." / "Snapped!" / "Photo Sent!" -> `FACE_STATE_SPEAK` / `FACE_STATE_HAPPY`
    - "WiFi Setup" / "Starting..." -> `FACE_STATE_TEXT`
    - `display_status_idle()` -> `FACE_STATE_IDLE`

## Phase 4: Build Registration & Tool Support
- Add `"display/display_face.c"` to `main/CMakeLists.txt`.
- Add `oled_face` tool in `main/tools/tool_registry.c` so the AI agent loop can trigger emotions.
- Compile firmware using `espet-build-flash` and verify binary fits partition table.
- Update `.agent/SYSTEM_OVERVIEW.md`.

