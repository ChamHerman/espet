# Implementation Plan: Wi-Fi Reset Replan & On-Chip Speech Recognition Analysis

Restore previous camera + voice push-to-talk button behavior, implement voice-driven Wi-Fi reset (`forget_wifi` / `reset_wifi` tool), and provide feasibility evaluation of on-chip WakeNet/MultiNet models on ESP32-S3.

---

## 1. Goal Description
The user requested three actions:
1. **Revert Button 5s BLE Mechanism**: Restore the physical green button (GPIO 41) to its original multimodal behavior:
   - **Press / Short click**: Capture picture with OV2640 camera and send to Telegram / Dashboard.
   - **Long press / Hold**: Multimodal hold-to-speak (record voice while held, release to transcribe & query LLM).
   - Remove the 5-second hold timeout and BLE trigger from `button_handler` and `mimi.c`.
   - Preserve the successful Wi-Fi startup fixes (eliminated boot scan collision, WPA2/WPA3 PMF support, non-blocking `esp_timer`).
2. **Voice/Agent Wi-Fi Reset (`forget_wifi`)**: Replace the physical 5-second button reset with a voice/chat-driven command. When the user tells ESPET `"forget wifi"` or `"reset wifi"`:
   - The LLM calls a new local tool `forget_wifi`.
   - The device confirms the request over TTS and chat.
   - After a 1.5-second delay to deliver the reply, it erases the Wi-Fi credentials from NVS (`wifi_manager_clear_credentials()`) and restarts into BLE Provisioning mode (`wifi_manager_trigger_ble_prov()`).
3. **On-Chip Speech Recognition Feasibility**: Address the user's question regarding deploying an on-chip speech recognition model for wake words and offline commands using ESP-Skainet (`esp-sr` WakeNet + MultiNet).

---

## 2. On-Chip Speech Model Feasibility & Architecture Analysis

### A. Can ESP32-S3 run a full arbitrary Speech-to-Text (STT) model?
> [!IMPORTANT]
> **No for general arbitrary dictation (Whisper/Kaldi/Vosk).**
> General STT models (such as Whisper-tiny or Kaldi) require 40MB–200MB+ model weights and tens of GFLOPs of floating-point matrix operations. ESP32-S3 has 8MB PSRAM and 16MB Flash, so general arbitrary dictation cannot run entirely on the MCU.

### B. Can ESP32-S3 run an On-Chip Wake Word Model (WakeNet)?
> [!TIP]
> **YES! WakeNet runs completely offline on ESP32-S3 with 0% cloud dependency.**
> - **Technology**: Espressif's **WakeNet** (`esp-sr` component, already in `managed_components/espressif__esp-sr`).
> - **Partition**: A dedicated 2MB `model` partition (`0xDF0000`, 2MB) is already reserved in `partitions.csv` and compiled as `srmodels.bin`.
> - **Hardware Acceleration**: Optimized with Xtensa PIE / TIE vector DSP instructions. Consumes only ~300KB–1.5MB of Flash/RAM and <5% CPU.
> - **Pre-trained models provided by Espressif**:
>   - English: `"Hi ESP"` (configured as `CONFIG_SR_WN_WN9_HIESP=y` in `sdkconfig.defaults.esp32s3`)
>   - Chinese: `"Nihao Zhiyin"` (你好知音), `"Hi Lexin"` (乐鑫)
> - **Limitation**: Custom wake words (e.g. "Mimi" or "ESPET") cannot be trained locally; Espressif requires uploading recordings to their online training portal.

### C. Can ESP32-S3 run Offline Speech Commands (MultiNet)?
> [!NOTE]
> **YES! MultiNet recognizes up to 100–300 offline commands with zero internet.**
> - MultiNet uses phonetic command grammars (e.g. `da kai dian deng`, `turn on the light`, `forget wifi`).
> - When WakeNet triggers, MultiNet listens for a command window and recognizes fixed phrases locally in ~300ms.

### D. Why ESPET Uses the Current Hybrid Voice Pipeline
1. **Wake Word Flexibility**: The project transitioned to energy VAD + Whisper on the companion backend (`/api/voice/transcribe`) because Whisper flexibly accepts custom wake words ("Mimi", "Hey Mimi", "米米") without vendor retraining.
2. **Open-Ended Agent Reasoning**: Because commands are natural language prompts for an LLM ReAct agent ("Summarize my emails", "Turn on the status LED and tell me the temperature"), rigid offline grammar sets like MultiNet cannot parse arbitrary intent.

---

## 3. User Review Required

> [!NOTE]
> 1. **Button Behavior**: The green button (GPIO 41) returns strictly to: short click = snapshot, hold = hold-to-speak multimodal query.
> 2. **Wi-Fi Reset**: Wi-Fi reset will be performed by voice or chat: say "Hey Mimi, forget wifi" or type "forget wifi" in chat. The agent will respond before clearing credentials from NVS and rebooting to BLE mode.
> 3. **Boot Startup**: The clean boot connection without scan collision and non-blocking reconnect timers remain active.

---

## 4. Proposed Changes

### Component 1: Button Handler (Revert 5s Hold)

#### [MODIFY] [button_handler.h](file:///c:/repository/espet/main/tools/button_handler.h)
- Revert `button_handler_init` to 2 arguments: `(button_press_down_cb_t down_cb, button_press_up_cb_t up_cb)`.
- Remove `button_long_press_cb_t` typedef.

#### [MODIFY] [button_handler.c](file:///c:/repository/espet/main/tools/button_handler.c)
- Remove `s_long_press_cb`, `long_press_handled`, and the 5-second periodic hold check in `button_task`.
- Restore standard release handling on button up.

#### [MODIFY] [mimi.c](file:///c:/repository/espet/main/mimi.c)
- Remove `on_camera_button_long_press()` function.
- Update `button_handler_init(on_camera_button_down, on_camera_button_up)`.

---

### Component 2: Local Agent Tool `forget_wifi`

#### [MODIFY] [tool_registry.c](file:///c:/repository/espet/main/tools/tool_registry.c)
- Implement `tool_forget_wifi_execute`:
  - Spawns `delayed_forget_wifi_task` (1.5s delay).
  - Returns JSON: `{"status": "success", "message": "Wi-Fi credentials erased from NVS. Device will restart into BLE Provisioning mode in 1.5 seconds."}`.
- In `delayed_forget_wifi_task`:
  - Calls `wifi_manager_clear_credentials()`.
  - Calls `wifi_manager_trigger_ble_prov()`.
- Register `forget_wifi` tool in `tool_registry_init()`:
  - Name: `"forget_wifi"`
  - Description: `"Forget/reset stored Wi-Fi credentials from NVS and restart device into BLE Provisioning mode. Call when user asks to forget Wi-Fi, reset network, or reconfigure Wi-Fi."`
  - Input schema: empty object `{}`.

---

### Component 3: Documentation & Memory Rule

#### [MODIFY] [.agent/SYSTEM_OVERVIEW.md](file:///c:/repository/espet/.agent/SYSTEM_OVERVIEW.md)
- Update button controls documentation (pure camera snap + voice PTT).
- Document `forget_wifi` local tool.
- Add change log entry.

---

## 5. Verification Plan

### Automated / Compilation Verification
1. Build firmware:
   ```powershell
   . C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1
   idf.py build
   ```
2. Verify zero warnings/errors and check binary size.

### Manual Verification
1. **Button Verification**:
   - Short click GPIO 41: Verifies camera snapshot captured and uploaded.
   - Long press & speak GPIO 41: Verifies voice recording captured while holding, transcribed and answered on release.
   - Holding past 5s: Verifies it does NOT reset the device.
2. **Voice/Agent Wi-Fi Reset Verification**:
   - Speak "Hey Mimi, forget wifi" or send "forget wifi" in chat.
   - Verify agent replies confirming Wi-Fi reset.
   - Verify device clears credentials from NVS, shows BLE Setup on OLED, and restarts into BLE Provisioning mode (`PROV_ESPET_XXXX`).
