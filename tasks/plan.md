# Implementation Plan: ESPET Hardware GPIO Tools & Dual Button Component Integration

## Overview
This plan details the step-by-step task breakdown for implementing ESP32-S3 hardware drivers and tools in `espet`. It covers SSD1306 OLED display pins, White LED control, I2S shared audio clocks (Mic + Amp), White Push Button (Camera Snap), and Green Push Button (Push-To-Talk Mic Recording).

## Architecture Decisions
- **Shared I2S Clocks**: GPIO 41 (BCLK) and GPIO 42 (WS) are shared in parallel between INMP441 mic and MAX98357A amp.
- **Dedicated Data Lines**: GPIO 1 (Mic SD input) and GPIO 2 (Amp DIN output).
- **Push-To-Talk (PTT)**: GPIO 39 (Green Button) uses `GPIO_INTR_ANYEDGE` interrupt to start recording on press (LOW) and stop on release (HIGH).
- **Camera Snap**: GPIO 38 (White Button) uses falling-edge interrupt to trigger JPEG frame capture from OV2640 camera.
- **Display**: GPIO 21 (SCL) & GPIO 47 (SDA) dedicated to SSD1306 OLED.

## Task Breakdown

### Phase 1: Foundation (GPIO Policy & Display)

#### Task 1: Update GPIO Policy Allowlist
**Description:** Add pins 1, 2, 21, 38, 39, 40, 41, 42, 47 to the GPIO policy CSV allowlist in `main/tools/gpio_policy.h`.

**Acceptance criteria:**
- [ ] `MIMI_GPIO_ALLOWED_CSV` contains `"1,2,21,38,39,40,41,42,47"`.
- [ ] `gpio_policy_pin_is_allowed()` returns true for these pins and false for reserved pins (33-37, 19, 20).

**Verification:**
- [ ] Build succeeds: `idf.py build`

**Dependencies:** None
**Files likely touched:** `main/tools/gpio_policy.h`
**Estimated scope:** Small (1 file)

#### Task 2: Configure OLED I2C Pins (GPIO 21 & 47)
**Description:** Update `tool_oled.c` to use `GPIO 21` (SCL) and `GPIO 47` (SDA) for the SSD1306 I2C master driver.

**Acceptance criteria:**
- [ ] `I2C_MASTER_SCK_IO` is set to `21`.
- [ ] `I2C_MASTER_SDA_IO` is set to `47`.
- [ ] `oled_print` tool initializes I2C on GPIO 21/47 without errors.

**Verification:**
- [ ] Build succeeds: `idf.py build`

**Dependencies:** Task 1
**Files likely touched:** `main/tools/tool_oled.c`
**Estimated scope:** Small (1 file)

### Checkpoint 1: Foundation
- [ ] Firmware compiles cleanly with updated GPIO allowlist and OLED pin mapping.

---

### Phase 2: Dual Hardware Buttons (White Snap & Green PTT Mic)

#### Task 3: Implement Dual Button Interrupt Handlers
**Description:** Create `button_handler.h` and `button_handler.c` to handle hardware edge interrupts for GPIO 38 (White Camera Snap) and GPIO 39 (Green PTT Mic).

**Acceptance criteria:**
- [ ] GPIO 38 configured with internal pull-up and falling-edge interrupt -> triggers camera snap event on press.
- [ ] GPIO 39 configured with internal pull-up and `GPIO_INTR_ANYEDGE` interrupt -> starts mic recording on press (LOW), stops mic recording on release (HIGH).
- [ ] Debounce logic prevents double-triggering.

**Verification:**
- [ ] Build succeeds: `idf.py build`

**Dependencies:** Task 1
**Files likely touched:**
- `main/tools/button_handler.h`
- `main/tools/button_handler.c`
- `main/tools/tool_gpio.c`
**Estimated scope:** Medium (3 files)

#### Task 4: Configure White Status LED (GPIO 40)
**Description:** Ensure `tool_gpio.c` supports GPIO 40 as an output pin via `gpio_write({"pin": 40, "state": 1})`.

**Acceptance criteria:**
- [ ] `gpio_write` sets GPIO 40 to HIGH (1) or LOW (0) cleanly.
- [ ] Status log confirms pin 40 output level changes.

**Verification:**
- [ ] Build succeeds: `idf.py build`

**Dependencies:** Task 1
**Files likely touched:** `main/tools/tool_gpio.c`
**Estimated scope:** Small (1 file)

### Checkpoint 2: Dual Buttons & Status LED
- [ ] Hardware button interrupts registered cleanly on boot.
- [ ] White LED output toggles without error.

---

### Phase 3: I2S Audio Drivers & Tools (Mic & Amp)

#### Task 5: Implement I2S Shared Audio Driver & Playback Tool
**Description:** Create `tool_audio.h` and `tool_audio.c` for MAX98357A speaker playback tool `audio_play_tone` on GPIO 41 (BCLK), GPIO 42 (WS), GPIO 2 (DIN).

**Acceptance criteria:**
- [ ] I2S driver initialized on GPIO 41, 42, 2.
- [ ] `audio_play_tone` tool registered in `tool_registry.c`.
- [ ] Generates a clean 444Hz test tone through MAX98357A speaker.

**Verification:**
- [ ] Build succeeds: `idf.py build`

**Dependencies:** Task 1
**Files likely touched:**
- `main/tools/tool_audio.h`
- `main/tools/tool_audio.c`
- `main/tools/tool_registry.c`
**Estimated scope:** Medium (3 files)

#### Task 6: Implement INMP441 Mic Stream & Record Tool
**Description:** Implement audio recording stream capture on GPIO 41 (SCK), GPIO 42 (WS), GPIO 1 (SD) for PTT button handler and `audio_record_test` tool.

**Acceptance criteria:**
- [ ] INMP441 audio samples read via I2S DMA.
- [ ] Green PTT button press streams audio into buffer until release.
- [ ] `audio_record_test` tool captures 3 seconds of audio and reports peak RMS amplitude.

**Verification:**
- [ ] Build succeeds: `idf.py build`

**Dependencies:** Task 5
**Files likely touched:**
- `main/tools/tool_audio.c`
- `main/tools/tool_audio.h`
**Estimated scope:** Medium (2 files)

### Checkpoint 3: I2S Audio Complete
- [ ] Both audio tools (`audio_play_tone` and `audio_record_test`) registered and compiling cleanly.

---

### Phase 4: Integration & Manual Telegram Testing Verification

#### Task 7: Create Manual Telegram & Physical Hardware Test Guide
**Description:** Document exact physical button actions and Telegram bot prompts in `docs/telegram_hardware_testing.md` to verify all components end-to-end.

**Acceptance criteria:**
- [ ] Documentation covers White Snap button, Green PTT button, White LED, OLED, and Audio tools.
- [ ] Includes expected Telegram bot text responses and log outputs.

**Verification:**
- [ ] Verification steps match firmware implementation.

**Dependencies:** Tasks 1-6
**Files likely touched:** `docs/telegram_hardware_testing.md`
**Estimated scope:** Small (1 file)

## Risks and Mitigations
| Risk | Impact | Mitigation |
|------|--------|------------|
| I2S clock clash between Mic and Speaker | Medium | Initialize I2S peripheral with standard clock configuration and shared master clock registers |
| Button mechanical bounce | Low | Implement 30ms software debouncing timer in button interrupt handler |
| GPIO policy rejection | Low | Updated `MIMI_GPIO_ALLOWED_CSV` in Task 1 before testing tool execution |
