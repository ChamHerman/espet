# Implementation Plan: Fix Microphone & Camera Concurrency & Wake-Word Lockout in ESPET

## Problem Summary & Goal Description
When using ESPET in hands-free voice mode ("Hey ESPET"), the wake-word listener works for the first few turns. However, when the user issues a command that invokes a camera snapshot (e.g. "what do you see?", "look at me", "take a picture") or any long-running ReAct tool execution, the passive voice receiver immediately becomes deaf. During this time, saying the wake word ("Hey ESPET") produces no response, despite the OLED showing the idle face animation and the serial console continuing to print the `[listen] 5s` debug report. After 1 to 2 minutes, the device suddenly recovers and detects speech again.

### Root Causes Identified
1. **Full-Turn Voice Muting via `s_busy` in `voice/voice_client.c`**:
   - When a voice command is transcribed, `s_busy` is flagged as `true`.
   - `voice_task` sees `s_busy == true` and enters an active sleep loop (`vTaskDelay(50); continue;`), bypassing VAD calculations and wake-word checks.
   - `s_busy` is only reset when `voice_client_speak()` finishes playing the final reply over the speaker (or when the 60-second watchdog timer in `voice_task` force-resets it).
   - Calling `take_photo` triggers a two-round LLM chain plus camera capture and Telegram photo upload over TLS, taking 30–60+ seconds. During this entire time, the mic is blocked even though no speaker audio is playing.
   - The 60-second watchdog is the exact reason why the device seemed to "come back to normal after a few minutes".

2. **Unstopped Manual Recording State Leak in `main/mimi.c`**:
   - In `on_camera_button_down()`, `tool_audio_manual_record_start()` sets `s_recording = true`.
   - If a button event is noise or shorter than 80 ms, `button_handler.c` calls `on_camera_button_up(0)`.
   - `on_camera_button_up(0)` cleans up the camera frame buffer but **fails to call `tool_audio_manual_record_stop()`**.
   - `s_recording` remains `true` indefinitely.
   - `tool_audio_is_busy()` (which returns `s_recording || s_playing`) returns `true` permanently.
   - Unlike `s_busy`, `tool_audio_is_busy()` has no watchdog timer, permanently bricking the wake-word listener until reboot.

3. **INMP441 L/R Auto-Switch Asymmetric Latch in `tools/tool_audio.c`**:
   - In `mic_feeder_task`, if high current draw (e.g. camera flash or sensor burst) causes `rms_l > 25000`, the driver auto-switches `s_active_slot = 1` (Right channel).
   - On standard mono wiring with L/R tied to GND, the Right channel is floating/silent.
   - The switch-back condition requires `rms_r > 25000`, which never occurs on a silent channel, latching the microphone into permanent silence.

4. **Missing Interruption / Barge-In Support**:
   - When the agent is busy with an in-flight tool or LLM round, there is no mechanism to abort the in-flight work when the user speaks a new command.

---

## User Review Required

> [!IMPORTANT]
> **Interruption / Barge-in Behavior**: When a user says "Hey ESPET" while an existing tool or LLM call is in-flight, the system will immediately abort the in-flight execution, cancel pending speaker playback of the obsolete response, and switch focus directly to the new command.

> [!NOTE]
> **Acoustic Echo Protection**: To prevent the speaker's own output from triggering the wake word (self-wake loop), microphone input will be muted **only while audio is actively playing through the speaker** plus a 700 ms post-playback echo cooldown. The mic will remain active while the CPU/network/camera are working.

---

## Proposed Changes

### Component 1: Voice Client Pipeline (`main/voice/voice_client.c`, `main/voice/voice_client.h`)

#### [MODIFY] `main/voice/voice_client.h`
- Add API functions to check and clear in-flight status and support abort signaling:
  - `void voice_client_request_abort(void);`
  - `bool voice_client_is_abort_requested(void);`
  - `void voice_client_clear_abort(void);`

#### [MODIFY] `main/voice/voice_client.c`
- Decouple `s_busy` from basic VAD listening:
  - In `voice_task`: change the pause condition so that it does **not** pause simply because the agent is thinking (`s_busy`).
  - Pause `voice_task` **only** when `tool_audio_is_speaker_busy() || mic_guard_active() || tool_audio_is_capturing()`.
- When a wake word is detected while `s_busy` is already `true`:
  - Log `*** WAKE WORD INTERRUPT DETECTED ***`.
  - Trigger `voice_client_request_abort()`.
  - Abort any in-flight playback or pending TTS.
- Lower the thinking watchdog from 60 seconds to 30 seconds as a secondary safety guard.

---

### Component 2: Agent ReAct Loop & Interruption (`main/agent/agent_loop.c`)

#### [MODIFY] `main/agent/agent_loop.c`
- In `agent_loop_task`:
  - Check `voice_client_is_abort_requested()` before and after every LLM call (`llm_chat_tools`) and before/after tool execution.
  - If abort is requested:
    - Log `[AGENT] Turn aborted by user voice interruption`.
    - Drop the current response; do not push obsolete answers to `s_outbound_queue`.
    - Clear abort flag (`voice_client_clear_abort()`).
    - Immediately loop to pop the fresh voice message from `s_inbound_queue`.

---

### Component 3: Button Handler & Manual Record State (`main/mimi.c`, `main/tools/button_handler.c`)

#### [MODIFY] `main/mimi.c`
- In `on_camera_button_up(uint32_t hold_duration_ms)`:
  - Ensure `tool_audio_manual_record_stop()` is **always called** even if `hold_duration_ms == 0` (cancels noise/glitch and resets `s_recording = false`).
  ```c
  if (hold_duration_ms == 0) {
      /* Always stop audio recording so s_recording is not leaked! */
      size_t dummy_samples = 0;
      uint32_t dummy_dur = 0;
      double dummy_rms = 0.0;
      tool_audio_manual_record_stop(&dummy_samples, &dummy_dur, &dummy_rms);

      if (s_snap_fb) {
          tool_camera_return_picture(s_snap_fb);
          s_snap_fb = NULL;
      }
      display_status_idle();
      return;
  }
  ```

---

### Component 4: I2S Audio Driver & Mic Feeder (`main/tools/tool_audio.c`, `main/tools/tool_audio.h`)

#### [MODIFY] `main/tools/tool_audio.c`
- Add a safety watchdog to `tool_audio_is_busy()`:
  - If `s_recording` has been active for longer than 35 seconds without a stop call, automatically disarm it and log a warning.
- Fix the INMP441 L/R auto-switch latch:
  - Prevent spurious latching onto a floating channel when `s_active_slot == 0`.
  - If auto-switch is triggered by an electrical transient, allow graceful fallback to Left slot if Right slot exhibits near-zero energy.

---

## Verification Plan

### Automated / Build Verification
- Run clean build using ESP-IDF v5.5 toolchain:
  ```powershell
  cmd /c "%IDF_PATH%\export.bat && idf.py build"
  ```
- Verify binary size fits comfortably in the flash partitions.

### Manual Verification
1. **Passive Wake-Word Test During Tool Execution**:
   - Say: `"Hey ESPET, take a photo"`.
   - Observe OLED showing camera action.
   - While ESPET is uploading to Telegram / calling the LLM, say `"Hey ESPET"` again.
   - **Expected Result**: Wake word triggers immediately without waiting 1–2 minutes; agent acknowledges the new command.
2. **Noise / Glitch Button Resiliency**:
   - Short-tap the button (<80ms) or play loud speaker audio.
   - Verify that `tool_audio_is_busy()` remains `false` and `[listen] 5s` shows normal non-zero RMS values and continues listening.
3. **Serial Console Log Check**:
   - Monitor `idf.py monitor` for `[listen] 5s` report.
   - Confirm `paused/empty` stays low during thinking/camera turns and only increases during actual speaker playback.
