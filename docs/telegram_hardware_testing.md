# ESPET Hardware & Telegram Bot Manual Testing Guide

This guide provides step-by-step instructions to test every connected hardware component using physical button gestures and Telegram bot commands.

---

## 📌 Hardware Pin Allocation Summary

| Component | Hardware Pin | Protocol / Function | Test Method |
|---|---|---|---|
| **SSD1306 OLED SCL** | **GPIO 21** | I2C Clock | Telegram Prompt / `oled_print` |
| **SSD1306 OLED SDA** | **GPIO 47** | I2C Data | Telegram Prompt / `oled_print` |
| **WHITE Push Button** | **GPIO 38** | Falling Edge Interrupt | **Physical Press** (Camera Snap) |
| **GREEN Push Button** | **GPIO 39** | Any-Edge Interrupt (PTT) | **Physical Press & Hold** (Mic Recording) |
| **WHITE Status LED** | **GPIO 40** | Output (HIGH/LOW) | Telegram Prompt / `gpio_write` |
| **INMP441 + MAX98357A** | **GPIO 41** | Shared I2S Bit Clock (`BCLK`/`SCK`) | Automatic / Shared Clock Line |
| **INMP441 + MAX98357A** | **GPIO 42** | Shared I2S Frame Sync (`WS`/`LRC`) | Automatic / Shared Clock Line |
| **MAX98357A Speaker** | **GPIO 2** | I2S Data Output (`DIN`) | Telegram Prompt / `audio_play_tone` |
| **INMP441 Microphone** | **GPIO 1** | I2S Data Input (`SD`) | PTT Button / `audio_record_test` |

---

## 🧪 1. Physical Hardware Button Tests

### A. WHITE Button (GPIO 38) — Camera Snap
1. **Action**: Press the **White Push Button**.
2. **Expected System Log**:
   ```
   I (button_handler): White Button Pressed (GPIO 38): Camera Snap Triggered
   ```
3. **Expected Behavior**: Triggers OV2640 JPEG capture and dispatches notification/image preview to Telegram.

### B. GREEN Button (GPIO 39) — Push-To-Talk (PTT) Mic Recording
1. **Action**: Press and **HOLD** the **Green Push Button**.
2. **Expected System Log**:
   ```
   I (button_handler): Green Button (GPIO 39): Pressed (Start PTT Rec)
   I (tool_audio): PTT Recording started (INMP441 GPIO 1)
   ```
3. **Action**: Speak into the INMP441 microphone (e.g., "Hello ESPET"), then **RELEASE** the button.
4. **Expected System Log**:
   ```
   I (button_handler): Green Button (GPIO 39): Released (Stop PTT Rec)
   I (tool_audio): PTT Recording stopped and submitted
   ```
5. **Expected Behavior**: Audio stream is captured during press, stopped on release, and sent to faster-whisper STT for processing.

---

## 🤖 2. Telegram Bot Command Tests

Connect to your Telegram bot channel and send the following prompts:

### A. WHITE Status LED Test (GPIO 40)
- **Send Prompt**: `"Turn on the white LED"`
  - *Expected Reply*: `"Pin 40 set to HIGH"` (White LED lights UP).
- **Send Prompt**: `"Turn off the white LED"`
  - *Expected Reply*: `"Pin 40 set to LOW"` (White LED turns OFF).

### B. SSD1306 OLED Display Test (GPIO 21 & 47)
- **Send Prompt**: `"Display 'Hello ESPET' on the OLED screen"`
  - *Expected Reply*: Confirmation message from `oled_print`; text appears on physical 0.96" OLED screen.

### C. MAX98357A Speaker Test (GPIO 41, 42, 2)
- **Send Prompt**: `"Play audio test tone on speaker"`
  - *Expected Reply*: Confirmation message from `audio_play_tone`; audible 444Hz beep tone plays from the 3W speaker.

### D. INMP441 Microphone Test (GPIO 41, 42, 1)
- **Send Prompt**: `"Test microphone recording"`
  - *Expected Reply*: Json output with sample count and non-zero `rms_level` verifying microphone audio capture.

### E. Check All GPIO Pin States
- **Send Prompt**: `"Read all GPIO pins"`
  - *Expected Reply*:
    ```
    GPIO states: 1=HIGH, 2=LOW, 21=HIGH, 38=HIGH, 39=HIGH, 40=LOW, 41=LOW, 42=LOW, 47=HIGH
    ```
    *(Note: Pins 38 and 39 show HIGH when unpressed due to internal pull-ups, and LOW when physically pressed).*
