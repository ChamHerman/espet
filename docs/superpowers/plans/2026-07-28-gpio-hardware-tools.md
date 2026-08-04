# ESPET Hardware GPIO Tools Implementation Plan (Updated)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement C firmware tools and hardware event handlers on ESP32-S3 for SSD1306 OLED, White LED, INMP441 Mic, MAX98357A Audio Amplifier, and Dual Push Buttons (White Button for Camera Snap/Capture, Green Button for Push-to-Talk Voice Recording), fully testable via Telegram bot and physical button presses.

**Architecture:** 
- `GPIO 38` (White Button): Interrupt/Polling trigger for OV2640 camera frame capture.
- `GPIO 39` (Green Button): Push-To-Talk (PTT) interrupt handler — starts INMP441 mic recording on press (LOW), stops and submits recording on release (HIGH).
- Tool registry extended with I2S audio tools (`audio_play_tone`, `audio_record_test`), OLED tools (`oled_print`), and GPIO tools. Telegram bot routes messages to ReAct agent loop and reports button trigger events.

**Tech Stack:** ESP-IDF v5.5+, FreeRTOS GPIO interrupts/queue, I2C driver (`driver/i2c.h`), I2S driver (`driver/i2s_std.h`), GPIO driver (`driver/gpio.h`), cJSON.

## Global Constraints

- **Pin Allocation Map**:
  - `GPIO 21`: SSD1306 OLED SCL (I2C Clock)
  - `GPIO 47`: SSD1306 OLED SDA (I2C Data)
  - `GPIO 39`: GREEN Push Button (Push-to-Talk Mic Recording: Press = Start Rec, Release = Stop Rec)
  - `GPIO 38`: WHITE Push Button (Camera Snap / Image Capture Trigger)
  - `GPIO 40`: WHITE LED (Output)
  - `GPIO 41`: INMP441 Mic SCK + MAX98357A Amp BCLK (Shared I2S Bit Clock)
  - `GPIO 42`: INMP441 Mic WS + MAX98357A Amp LRC (Shared I2S Word Select)
  - `GPIO 2`: MAX98357A Amp DIN (I2S Data Output to Speaker)
  - `GPIO 1`: INMP441 Mic SD (I2S Data Input from Microphone)
- **Do not modify reserved pins**: Never access GPIO 33-37 (PSRAM), GPIO 19/20 (USB OTG), or GPIO 43/44 (Console UART).

---

### Task 1: Update GPIO Policy & Pin Allowlist

**Files:**
- Modify: `main/tools/gpio_policy.h:7-10`

**Interfaces:**
- Consumes: None
- Produces: Updated `MIMI_GPIO_ALLOWED_CSV` defining permitted user-facing GPIO pins.

- [ ] **Step 1: Update MIMI_GPIO_ALLOWED_CSV macro**
Update `MIMI_GPIO_ALLOWED_CSV` in `main/tools/gpio_policy.h`:
```c
#define MIMI_GPIO_ALLOWED_CSV   "1,2,21,38,39,40,41,42,47"
```

- [ ] **Step 2: Commit**
```bash
git add main/tools/gpio_policy.h
git commit -m "feat(gpio): update gpio policy allowlist for espet pin map"
```

---

### Task 2: Configure OLED Display I2C Pins

**Files:**
- Modify: `main/tools/tool_oled.c:11-15`

**Interfaces:**
- Consumes: `driver/i2c.h`, `ssd1306.h`
- Produces: `oled_print` tool running on `GPIO 21` (SCL) and `GPIO 47` (SDA).

- [ ] **Step 1: Update SCL and SDA pin constants**
Modify `main/tools/tool_oled.c`:
```c
#define I2C_MASTER_SCK_IO           21
#define I2C_MASTER_SDA_IO           47
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          400000
```

- [ ] **Step 2: Commit**
```bash
git add main/tools/tool_oled.c
git commit -m "fix(oled): update i2c scl to gpio21 and sda to gpio47"
```

---

### Task 3: Implement Dual Button Event Handler (White = Camera Snap, Green = Push-To-Talk Mic)

**Files:**
- Create: `main/tools/button_handler.h`
- Create: `main/tools/button_handler.c`
- Modify: `main/tools/tool_gpio.c`

**Interfaces:**
- Consumes: `driver/gpio.h`, FreeRTOS queues
- Produces: Hardware interrupt / edge detector for GPIO 38 (White Snap Button) and GPIO 39 (Green PTT Mic Button).

- [ ] **Step 1: Define button_handler.h**
```c
#pragma once
#include "esp_err.h"

#define GPIO_BTN_WHITE_SNAP  38  /* Press -> Snap Camera Image */
#define GPIO_BTN_GREEN_PTT   39  /* Press -> Start Mic Rec, Release -> Stop Mic Rec */

esp_err_t button_handler_init(void);
```

- [ ] **Step 2: Implement button_handler.c**
- Configure GPIO 38 as input with internal pull-up and falling-edge interrupt (White Snap Button).
- Configure GPIO 39 as input with internal pull-up and both-edge interrupt (`GPIO_INTR_ANYEDGE` for Green PTT Button).
- When GPIO 39 falls LOW (pressed): Start I2S mic recording stream.
- When GPIO 39 rises HIGH (released): Stop recording stream and submit audio for STT transcription / Telegram notification.
- When GPIO 38 falls LOW (pressed): Trigger camera JPEG frame capture and send status event.

- [ ] **Step 3: Commit**
```bash
git add main/tools/button_handler.h main/tools/button_handler.c main/tools/tool_gpio.c
git commit -m "feat(button): implement hardware handlers for White camera snap and Green PTT mic recording"
```

---

### Task 4: Implement I2S Audio Drivers & Tools (Mic & Amp)

**Files:**
- Create: `main/tools/tool_audio.h`
- Create: `main/tools/tool_audio.c`
- Modify: `main/tools/tool_registry.c`

**Interfaces:**
- Consumes: `driver/i2s_std.h`, `cJSON.h`
- Produces: `audio_play_tone` tool (tests MAX98357A speaker), `audio_record_test` tool (tests INMP441 mic).

- [ ] **Step 1: Define tool_audio.h**
```c
#pragma once
#include "esp_err.h"
#include "tool_registry.h"

#define I2S_SHARED_BCLK_IO  41
#define I2S_SHARED_WS_IO    42
#define I2S_AMP_DIN_IO      2
#define I2S_MIC_SD_IO       1

esp_err_t tool_audio_init(void);
extern mimi_tool_t audio_play_tone_tool;
extern mimi_tool_t audio_record_test_tool;
```

- [ ] **Step 2: Implement tool_audio.c**
Implement sine-wave tone generation for MAX98357A speaker test on GPIO 41 (BCLK), 42 (WS), 2 (DIN).
Implement mic recording stream handler for INMP441 on GPIO 41 (SCK), 42 (WS), 1 (SD).

- [ ] **Step 3: Register audio tools in tool_registry.c**
```c
#include "tools/tool_audio.h"
...
tool_audio_init();
register_tool(&audio_play_tone_tool);
register_tool(&audio_record_test_tool);
```

- [ ] **Step 4: Commit**
```bash
git add main/tools/tool_audio.h main/tools/tool_audio.c main/tools/tool_registry.c
git commit -m "feat(audio): add i2s tone generator and mic recording tools"
```

---

### Task 5: Manual Telegram Bot & Physical Hardware Verification Suite

**Files:**
- Create: `docs/telegram_hardware_testing.md`

**Interfaces:**
- Consumes: Telegram Bot (`channels/telegram`), Hardware physical buttons
- Produces: Test verification protocol for both physical buttons and Telegram commands.

- [ ] **Step 1: Create Telegram & Hardware Testing Guide**
1. **White Button (GPIO 38 - Camera Snap)**:
   - Physical Action: Press White Button.
   - Expected Result: System logs `"White button pressed: Camera capture triggered"`, captures frame, and sends notification / image preview to Telegram.
2. **Green Button (GPIO 39 - Push-To-Talk Mic)**:
   - Physical Action: Press and HOLD Green Button -> speak -> RELEASE Green Button.
   - Expected Result: System records mic audio while pressed, stops on release, processes audio, and notifies Telegram with recorded audio status.
3. **White LED (GPIO 40)**:
   - Telegram message: `"Turn on white LED"` / `"Turn off white LED"`.
4. **SSD1306 OLED (GPIO 21 & 47)**:
   - Telegram message: `"Display 'Hello ESPET' on OLED"`.
5. **MAX98357A Speaker (GPIO 41, 42, 2)**:
   - Telegram message: `"Play audio test tone"`.

- [ ] **Step 2: Commit**
```bash
git add docs/telegram_hardware_testing.md
git commit -m "docs: add physical button and telegram hardware testing guide"
```
