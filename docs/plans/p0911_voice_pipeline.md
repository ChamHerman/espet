# Embodied AI Voice Pipeline: Mic Primary Input with Telegram Bridge & Industry VAD Constraints

## Context
ESPET operates as an embodied personal AI agent on ESP32-S3. The user requested the microphone (INMP441) as primary input with Telegram as backup. Because the physical speaker (MAX98357A) is not currently connected for hardware testing, wake detections and agent responses must bridge to the bound Telegram chat while maintaining TTS integration for future speaker availability. The listening window requires industry-standard timing constraints (Alexa/Xiaozhi 3-tier VAD model) to prevent cutting off conversational questions or streaming dead silence to cloud STT.

## Approach

### 1. Update Voice Timing Constraints (`main/voice/voice_client.c`)
Adopt the Industry Conversational Standard:
- Set `CMD_MAX_MS` to `8000` (8.0s hard cap; at 16 kHz 16-bit mono = 128,000 samples / 256 KB, safely within `AUDIO_MAX_SAMPLES` PSRAM allocation).
- Set `CMD_LEAD_MS` to `3500` (3.5s wait-for-speech / initial silence timeout).
- Set `CMD_SILENCE_MS` to `1200` (1.2s trailing silence endpointing VAD; allows natural pauses).
- Define `CMD_MIN_SPEECH_MS` as `300` (minimum 300ms continuous speech to filter clicks/coughs).
- Update `CMD_MAX_SAMPLES` to `(TOOL_AUDIO_SAMPLE_RATE * CMD_MAX_MS / 1000)`.

### 2. Implement 3-Tier VAD State Machine in `voice_run_command()` (`main/voice/voice_client.c`)
Refactor the capture loop in `voice_run_command()`:
- Track:
  - `start_ms = esp_timer_get_time() / 1000;`
  - `last_speech_ms = 0;`
  - `speech_duration_ms = 0;`
  - `heard_speech = false;`
- In read loop:
  - If `rms >= vad_trigger_threshold()`: mark `heard_speech = true`, update `last_speech_ms`, increment `speech_duration_ms += VAD_CHUNK_MS`.
  - **Tier 1 (Wait-for-speech timeout)**: `if (!heard_speech && (now - start_ms >= CMD_LEAD_MS))`
    - Abort immediately without calling STT.
    - Send Telegram notification: `telegram_send_to_last_chat("⏱️ Listening timed out (no speech heard).")`.
    - Beep `tool_audio_beep(BEEP_TIMEOUT_HZ, BEEP_MS, BEEP_VOL)`.
    - Reset `s_busy = false`, `s_busy_start_ms = 0`, `display_status_idle()`, flush audio cache, return.
  - **Tier 2 (Endpointing trailing silence)**: `if (heard_speech && speech_duration_ms >= CMD_MIN_SPEECH_MS && (now - last_speech_ms >= CMD_SILENCE_MS))`
    - End-of-utterance reached; break capture loop.
  - **Tier 3 (Hard cap)**: `if (now - start_ms >= CMD_MAX_MS)`
    - Break capture loop on 8.0s limit.
- Post-capture feedback:
  - If audio too short (< 500ms): send `telegram_send_to_last_chat("⚠️ Command too short or unclear.")`.
  - On valid transcription: send `telegram_send_to_last_chat(you_msg)` formatted as `🗣️ You: "<cmd_text>"`.
  - If STT fails or produces empty text: send `telegram_send_to_last_chat("⚠️ Didn't catch that. Please speak again.")`.

### 3. Telegram Feedback on Wake Detection (`main/voice/voice_client.c`)
In `voice_task()` wake detection block (`stt_res.wake_matched == true`):
- **Hybrid One-Shot Command** (`stt_res.text && strlen(stt_res.text) > 0`):
  - Send Telegram message:
    `🎤 Wake word matched!\n🗣️ You: "<stt_res.text>"`
  - Queue `stt_res.text` to message bus with `channel = MIMI_CHAN_SYSTEM`, `chat_id = "voice"`.
- **Two-Stage Wake Word Only** (no command text in wake utterance):
  - Send Telegram message:
    `🎤 I heard you! Listening for your command...`
  - Play ack ("Yes?"), flush cache, call `voice_run_command()`.

### 4. Bridge LLM Voice Responses to Telegram (`main/mimi.c`)
In `outbound_dispatcher_task()` (`main/mimi.c`):
- For `msg.channel == MIMI_CHAN_SYSTEM` and `strcmp(msg.chat_id, "voice") == 0`:
  - Format response message prefixed with `🤖 ESPET: `:
    ```c
    size_t len = strlen("🤖 ESPET: ") + strlen(msg.content) + 1;
    char *tlg_msg = (char *)malloc(len);
    if (tlg_msg) {
        snprintf(tlg_msg, len, "🤖 ESPET: %s", msg.content);
        telegram_send_to_last_chat(tlg_msg);
        free(tlg_msg);
    } else {
        telegram_send_to_last_chat(msg.content);
    }
    ```
  - Concurrently call `voice_client_speak(msg.content)` to invoke the TTS pipeline so audio output is active when speaker hardware is wired.

### 5. Telegram as Backup Input
- Incoming Telegram text messages continue to arrive via `MIMI_CHAN_CLOUD` (hybrid WebSocket gateway) or `MIMI_CHAN_TELEGRAM` (standalone polling).
- Replies to Telegram text messages continue to route back to Telegram via `cloud_link_send_text()` or `telegram_send_message()`. No collision with voice channel.

## Critical Files & Anchors
1. `main/voice/voice_client.c` (lines 80-87, 470-620, 840-880): Timing constants, 3-tier VAD in `voice_run_command()`, and wake-detection Telegram notifications.
2. `main/mimi.c` (lines 249-256): System channel outbound dispatcher routing to Telegram and TTS.
3. `main/channels/telegram/telegram_bot.c` (lines 665-680): `telegram_send_to_last_chat()` routing via `cloud_link_send_text()`.

## Verification
1. **Firmware Compilation**:
   Run ESP-IDF build command:
   ```cmd
   cmd.exe /c "set IDF_TOOLS_PATH=C:\Espressif\tools&& set IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v5.5.4\venv&& set IDF_PATH=C:\esp\v5.5.4\esp-idf&& set PYTHONIOENCODING=utf-8&& set PYTHONUTF8=1&& C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe C:\esp\v5.5.4\esp-idf\tools\idf.py build"
   ```
   Must compile with zero errors.
2. **Initial Silence Timeout Check**:
   - Trigger wake word ("Hey ESPET").
   - Remain completely silent.
   - Verify log: `[CMD] wait-for-speech timeout (3500 ms) — no speech detected`.
   - Verify Telegram receives: `⏱️ Listening timed out (no speech heard).`
   - Verify device returns to idle without sending audio to backend STT.
3. **End-to-End Voice Command to Telegram Check**:
   - Trigger wake word ("Hey ESPET").
   - Verify Telegram receives: `🎤 I heard you! Listening for your command...`
   - Speak command within 3.5s: "What is 2 plus 2?"
   - Pause for 1.2s; verify log: `[CMD] speech ended with 12xx ms trailing silence`.
   - Verify Telegram receives: `🗣️ You: "What is 2 plus 2?"`
   - Verify Telegram receives: `🤖 ESPET: 2 plus 2 is 4.`
   - Verify log shows `voice_client_speak` executed TTS fetch.
4. **Backup Telegram Text Check**:
   - Send text message directly from Telegram app: "Hello".
   - Verify ESPET processes and replies to Telegram normally.

## Assumptions & Contingencies
- **Assumptions**: Device is bound to Telegram via `espet-backend` (`cloud_link`) or standalone bot token. `s_last_chat_id` is restored from NVS or synced via `hello_ack`.
- **Contingency**: If device boots without active Telegram binding, `telegram_send_to_last_chat()` logs a warning and drops the chat send without crashing, while OLED status and TTS continue normal operation.
