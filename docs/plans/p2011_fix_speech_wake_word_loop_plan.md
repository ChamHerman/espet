# Fix Speech Wake-Word Re-detection & Premature Command Window Abort Plan

## Goal Description
Resolve the issue where speaking after the wake word ("Hey ESPET") causes the speech to be prematurely ignored, cut off, or treated as another wake-word utterance (`[wake] not matched ... ignoring ambient talk`) instead of being captured and processed as the intended command.

---

## Root Cause Analysis

Based on serial logs from the user's uploaded trace (`media_1795187062288_29b025a3.jpg`) and a full inspection of `main/voice/voice_client.c`, `main/agent/agent_loop.c`, and `main/tools/tool_audio.c`:

```
Log Timeline:
I (114185) voice: *** WAKE WORD ONLY - playing 'Yes?' and listening for command ***
I (114215) tool_audio: [SPK] WAV playback interrupted by user (0 bytes played!)
I (114785) voice: [CMD] Listening for command (countdown 15000 ms starts now after 'Yes?')...
I (114795) voice: [CMD] command window aborted by user interruption (after only 10ms!)
I (114795) voice: [CMD] abort flag set - discarding captured command
I (117115) voice: 🎤 [VAD] speech energy: rms=673 (frame 1/3, thr=420)
I (117245) voice: >>> SPEECH DETECTED (rms=663) - capturing utterance with 250ms pre-roll...
I (121125) voice: [utterance] silence timeout (1200 ms)
I (122605) voice: STT 1481 ms, 69316 -> 112 bytes: wake=0 (''), cmd='', raw='Remember I have an interview with Dr Wong tomorrow.'
I (122605) voice: [wake] not matched - raw='Remember I have an interview with Dr Wong tomorrow.' (ignoring ambient talk)
```

### 1. `s_busy` Overloading Caused Self-Interruption on Every Wake Word
In `main/voice/voice_client.c`:
- At line 1038, `s_busy = true;` was set when VAD speech was initially detected.
- When Whisper verified the wake word at line 1132:
  ```c
  if (s_busy) {
      ESP_LOGW(TAG, "*** WAKE WORD INTERRUPT: In-flight agent turn detected — requesting abort ***");
      voice_client_request_abort();
  }
  ```
- Because line 1038 set `s_busy = true`, `s_busy` was **always true** when any wake word matched.
- This caused `voice_client_request_abort()` to be invoked unconditionally on **every single wake word**, asserting `s_abort_requested = true`.

### 2. Uncleared Abort Flag Prematurely Aborted the Command Window
Because `s_abort_requested` was set during wake-word detection:
1. `ack_play()` tried to play *"Yes?"*, but `tool_audio_play_wav()` saw `s_abort_requested == true` and immediately halted (`WAV playback interrupted by user`, 0 bytes played).
2. `voice_run_command()` started listening for the command, but on its very first loop iteration (10 ms in at `114795`), it saw `voice_client_is_abort_requested() == true`.
3. It immediately broke out of the capture loop: `command window aborted by user interruption`, disarmed capture, and returned to idle `voice_task`.
4. **The user was just getting ready to speak**, but the command-listening window had already died!
5. When the user spoke ("Remember I have an interview with Dr Wong tomorrow."), the device was back in the idle wake-word listener (`voice_task`).
6. Because the command didn't start with "Hey ESPET", Whisper returned `wake=0`, and the log printed: `[wake] not matched ... (ignoring ambient talk)`.

---

## User Review Required

> [!IMPORTANT]
> The wake word should only trigger an interruption/abort if the **Core 1 agent loop** is actively executing an in-flight LLM query or running tool iterations. 
> To prevent cross-turn race conditions, turns will be assigned a monotonic **Turn ID**. An abort signal will strictly target the specific in-flight turn ID, making subsequent turns, acknowledgment playback, and command capture windows 100% immune to false abortion.

---

## Proposed Changes

```
┌────────────────────────────────────────────────────────────────────────┐
│                        DUAL-CORE TURN ARBITRATION                      │
│                                                                        │
│   CORE 0: voice_task (VAD & WakeNet)                                   │
│     1. User says "Hey ESPET"                                           │
│     2. Check: agent_loop_is_busy()?                                    │
│        ├─ YES: voice_client_request_abort_turn(current_turn_id)        │
│        └─ NO : Idle. Do NOT request abort.                             │
│     3. ack_play() ("Yes?" plays completely — not abortable)            │
│     4. voice_run_command()                                             │
│        - Captures speech (up to 15s or 1.2s silence)                   │
│        - Sends to Whisper STT                                          │
│        - Pushes cmd_text to message_bus_push_inbound                   │
│                                                                        │
│   CORE 1: agent_loop_task                                              │
│     1. Pops message from bus → my_turn_id = ++s_agent_turn_id           │
│     2. s_agent_turn_in_flight = true                                   │
│     3. If voice_client_is_turn_aborted(my_turn_id):                     │
│        - Halts LLM / tool calls, drops stale output                    │
│     4. Finished → s_agent_turn_in_flight = false                       │
└────────────────────────────────────────────────────────────────────────┘
```

---

### Component 1: `main/agent/` — Agent Loop Turn Tracking

#### [MODIFY] `main/agent/agent_loop.h`
- Declare:
  ```c
  bool agent_loop_is_busy(void);
  uint32_t agent_loop_get_current_turn_id(void);
  ```

#### [MODIFY] `main/agent/agent_loop.c`
- Add `s_agent_turn_in_flight` and `s_agent_current_turn_id`.
- Increment `s_agent_current_turn_id` when popping an inbound message.
- Update abort checks in the ReAct loop to check `voice_client_is_turn_aborted(my_turn_id)`.
- Clear `s_agent_turn_in_flight` upon turn completion or abort.

---

### Component 2: `main/voice/` — Targeted Turn Abort & Clean Command Window

#### [MODIFY] `main/voice/voice_client.h`
- Replace generic abort with turn-targeted abort:
  ```c
  void voice_client_request_abort_turn(uint32_t turn_id);
  bool voice_client_is_turn_aborted(uint32_t turn_id);
  void voice_client_clear_abort(void);
  ```

#### [MODIFY] `main/voice/voice_client.c`
- Include `agent/agent_loop.h`.
- In `voice_task()`:
  - Replace `if (s_busy)` with `if (agent_loop_is_busy())`:
    ```c
    if (agent_loop_is_busy()) {
        uint32_t in_flight_turn = agent_loop_get_current_turn_id();
        ESP_LOGW(TAG, "*** WAKE WORD INTERRUPT: In-flight agent turn #%u detected — requesting abort ***", (unsigned)in_flight_turn);
        voice_client_request_abort_turn(in_flight_turn);
    }
    ```
  - For the newly matched command:
    - In Case 1 (One-shot command): clear any residual abort flag before pushing to inbound queue.
    - In Case 2 (Wake word only): clear any residual abort flag before calling `ack_play()` and `voice_run_command()`.
- In `voice_run_command()`:
  - Ensure command capture runs continuously until trailing silence (1.2s) or 15s hard cap.
  - Remove bogus abort checks that triggered on self-abort.
  - After transcription, push command to `message_bus_push_inbound`.
  - Mark `s_busy = true` while the agent executes the turn, and only return to VAD wake listening after the agent response is delivered.

---

### Component 3: `main/tools/` — Speaker Playback Hardening

#### [MODIFY] `main/tools/tool_audio.c`
- In `tool_audio_play_wav()`:
  - Do not abort short acknowledgement files (`ack_play()` / "Yes?" sound).
  - Only allow aborts during conversational TTS playback (`voice_speak_internal`).

---

## Verification Plan

### Automated Compilation & Backend Tests
1. **Firmware Compilation**:
   ```powershell
   pwsh -Command ". C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1; idf.py build"
   ```
   Verify 0 warnings, 0 errors, binary size fits partition.
2. **Backend Regression Tests**:
   ```bash
   cd espet-backend
   python -m pytest tests/
   ```
   Verify 100% test pass rate.

### Manual Verification
1. Say "Hey ESPET" (wake-only).
   - Verify OLED shows `Listening... Say command`.
   - Verify *"Yes?"* plays completely without being truncated.
2. Speak a long sentence (e.g. "Remember I have an interview with Dr Wong tomorrow").
   - Verify log shows `[CMD] captured X samples ... speech ended with 1200 ms trailing silence`.
   - Verify STT transcribes the full sentence.
   - Verify the command is dispatched to the ReAct agent, NOT dropped as `[wake] not matched`.
3. Test Barge-In / Interruption:
   - While the agent is in the middle of executing a multi-tool query, say "Hey ESPET".
   - Verify the in-flight turn aborts cleanly and the new turn takes over.
