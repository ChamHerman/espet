# Fix Stale Captured Audio & Progressive Audio/STT Lockouts on `/stt`

## Goal Description
Resolve two field issues observed on the `/stt` Voice & Speaker Debug Bench (and screenshot):
1. **Stale / mismatched captured audio**: STT returns the right transcript (e.g., `"Hey expert, what do you hear man?"`, `3.641s / 29,127`), while the `<audio>` player shows an older or shorter clip (`0:02 / 0:02`) or wrong audio.
2. **Audio/STT stops working after a few test runs**: Repeated hardware captures, browser mic captures, or file tests progressively fail, hang for 20s, return `no_audio` / `task_fail`, or lock out mic/speaker state.

---

## Root Causes Identified

### 1. Why captured audio is wrong or does not update
- **Browser `<audio>` element never calls `.load()` & URLs lack cache-safe isolation (`espet-backend/static/stt.html`)**:
  - `loadLastCaptureAudioPreview()` assigns `$("stt-audio").src = url` without calling `$("stt-audio").load()`, and fetches `last-capture-audio` without `cache: "no-store"`. With `preload="none"`, Chromium can retain stale duration/buffer metadata (`0:02 / 0:02`) from an earlier capture.
  - `stopHwRecord()` mixes `resp.samples` (ESP32 pre-upsample 8 kHz sample count: `29,127`) with `diag.duration_s` (post-upsample 16 kHz WAV duration: `3.641s`) and re-runs Whisper a second time against the shared `data/last_capture.wav` file.
- **Shared global `data/last_capture.wav` race (`espet-backend/api/voice_router.py`, `main/channels/cloud_link/cloud_link.c`)**:
  - Every `POST /api/voice/transcribe` call—including background VAD ambient triggers—unconditionally overwrites the single `data/last_capture.wav` file non-atomically (`write_bytes`).
  - On the ESP32, `record_stop` (`cloud_link.c`) clears `s_recording = false` *before* `transcribe_task_fn` reads `s_pcm_buf` and uploads it. If VAD is active, `voice_task` immediately arms `tool_audio_capture_arm_preroll()`, overwriting `s_pcm_buf` and subsequently overwriting `data/last_capture.wav` with room noise.

### 2. Why audio/STT stops working after a few test runs
- **Unescaped JSON & 512-byte buffer overflow in `cloud_link.c`**:
  - `transcribe_task_fn` builds `record_result` using `snprintf(resp, sizeof(resp), "...\"transcript\":\"%s\"}", ...)`. Any `"` or `\` or newline from Whisper, or a transcript > ~400 bytes, emits malformed JSON. `cloud_router.py` hits `JSONDecodeError`, drops the frame, and `/api/cloud/record/stop` hangs for 20s until timeout.
- **Internal DRAM exhaustion from dynamic task creation (`cloud_link.c`)**:
  - `transcribe_task_fn` and `tts_say_task_fn` use `xTaskCreate(..., 8192, ...)` in internal DRAM (~40 KB total). Repeated STT/TTS requests alongside TLS/HTTP buffers fragment internal DRAM until `xTaskCreate` fails (`task_fail`) or `esp_http_client_init` fails.
- **`s_pcm_buf` / `s_recording` / `s_speaker_busy` races (`tool_audio.c`, `voice_client.c`)**:
  - `tool_audio_manual_record_start()` checks `s_playing` but **not** `s_speaker_busy` (active during TTS HTTP download), allowing recording to start while `mic_feeder_task` is parked -> 0 samples captured -> `no_audio`.
  - No ownership lock guards `s_pcm_buf` between `manual_record_stop()` and `transcribe_task_fn` completion, and `voice_task` can still call `tool_audio_capture_disarm()` mid-manual-recording if VAD was active.
- **Browser resource leaks & stale UI state (`stt.html`)**:
  - `btn-upload-stt` creates `new AudioContext()` without `try / finally { await tmpCtx.close(); }`—browsers cap active `AudioContext` instances (~6), causing audio decode/PC mic to stop working after ~6 uses/errors.
  - `URL.createObjectURL()` blobs are never revoked (`URL.revokeObjectURL`), and `hwRecording` has no `Force Reset / Cancel` recovery if the 30s ESP32 cap fires or a request times out.
- **Aggressive `backend_discovery_invalidate()` (`voice_client.c`, `backend_discovery.c`)**:
  - A single transient HTTP error drops `s_base_url` back to compiled-in `MIMI_BACKEND_URL` while waiting up to 33s for mDNS re-discovery.

---

## User Review Required

> [!IMPORTANT]
> **Two-layer fix (Backend/UI vs. ESP32 Firmware):**
> - **Part 1 (Backend + `/stt` UI)** takes effect immediately on backend restart with **zero firmware flashing** and fixes browser `AudioContext` leaks, `<audio>` `.load()` staleness, double-Whisper latency, atomic WAV capture isolation (`capture_id` ring), and UI/recording recovery.
> - **Part 2 (ESP32 Firmware)** permanently fixes the root hardware/RTOS lockouts (`cJSON` response building, PSRAM-backed `stt_trans`/`tts_say` worker stacks, `s_pcm_buf` ownership guard vs VAD, `s_speaker_busy` check, and `record_abort` WS frame), and **requires `idf.py build flash`** to take effect on the device.

> [!NOTE]
> Keeping `POST /api/voice/transcribe` and `POST /api/voice/tts` response contracts 100% backward compatible ensures zero regression to existing voice, Telegram, and camera flows.

---

## Proposed Changes

### Part 1: Backend & `/stt` Debug Page (No Firmware Flash Required)

#### [MODIFY] [voice_router.py](file:///c:/repository/espet/espet-backend/api/voice_router.py)
- Add an in-memory/on-disk **atomic capture ring** (`MAX_DEBUG_CAPTURES = 10` in `data/captures/` or memory) alongside atomic `os.replace` writes for `data/last_capture.wav` so concurrent requests never serve half-written WAVs.
- Give each `/api/voice/transcribe` capture a `capture_id` and track the **latest hardware capture metadata** (`capture_id`, `raw_text`, `text`, `wake_matched`, `wake_word`, `avg_logprob`, `no_speech_prob`, `rms_raw`, `rms_processed`, `samples_in`, `samples_out`, `duration_s`, `latency_ms`, `timestamp`).
- Add `GET /api/voice/debug/last-result` and `GET /api/voice/captures/{capture_id}` (with `Cache-Control: no-store`) so `/stt` fetches the **exact** audio + Whisper metrics from the hardware capture without running Whisper a second time!
- Only update `data/last_capture.wav` for real non-silent captures or explicit manual captures (do not overwrite a valid manual capture with `< 0.2s` or below-RMS noise, and keep `_transcribe_pcm_impl(..., save_capture=False)` on synthetic/regression runs).

#### [MODIFY] [cloud_router.py](file:///c:/repository/espet/espet-backend/api/cloud_router.py)
- In `POST /api/cloud/record/stop`, attach the latest server-side `voice_router` capture diagnostics (`capture_id`, `audio_url`, `avg_logprob`, `no_speech_prob`, `wake_matched`, `wake_word`, `raw_text`, `rms_processed`, `duration_s`) when available so `stt.html` gets both ESP32 stats and Whisper diagnostics in **one** roundtrip (eliminating the second `use_last_capture` Whisper call!).
- Add `POST /api/cloud/record/abort` (sends `{"type": "record_abort"}` or falls back cleanly if firmware doesn't implement it yet, clearing any stuck `_pending_record_start` / `_pending_record_stop` futures).

#### [MODIFY] [stt.html](file:///c:/repository/espet/espet-backend/static/stt.html)
- Fix `loadAudioPreview(url)`:
  - Fetch WAV with `{ cache: "no-store" }`, create a clean `Blob` URL, **revoke** the previous `Blob` URL via `URL.revokeObjectURL`, set `$("stt-audio").src = blobUrl`, and explicitly call `$("stt-audio").load()` so duration/scrubber always match the exact waveform shown!
- Fix `stopHwRecord()`:
  - Display pre-upsample ESP32 samples vs post-upsample WAV duration explicitly (`3.64s @ 16kHz | ESP32 raw: 29,127`) without re-running Whisper a second time when `record/stop` already returned server diagnostics; fetch the exact `audio_url` (`/api/voice/captures/{id}` or `/api/voice/last-capture-audio?t=...`).
- Fix `AudioContext` & `Blob` URL leaks:
  - Wrap `tmpCtx` in `btn-upload-stt` and `pcCtx` in `btn-pc-rec` inside `try { ... } finally { await ctx.close(); }` and clean up `MediaStream` tracks on error or unmount.
  - Revoke `lastTtsBlobUrl` before creating a new TTS `Blob` URL.
- Add a **"Force Reset / Abort Mic"** button + automatic timer/state reset when the 30s hardware capture cap hits or `record/stop` times out, so the UI never gets stuck out of sync with the ESP32.

---

### Part 2: ESP32 Firmware Audio & CloudLink Hardening (Requires Flash)

#### [MODIFY] [cloud_link.c](file:///c:/repository/espet/main/channels/cloud_link/cloud_link.c)
- Replace `snprintf(resp, 512, ...)` in `transcribe_task_fn` with `cJSON` (`cJSON_CreateObject` + `cJSON_PrintUnformatted`) so quotes, newlines, and long transcripts never break JSON or cause 20s timeouts.
- Return `raw_text` alongside `transcript` (or prefer `raw_text` for manual `/stt` debug) using `voice_client_transcribe_full()`, and release the manual capture buffer lock (`tool_audio_manual_capture_release()`) in `transcribe_task_fn` once upload completes.
- Replace dynamic `xTaskCreate(..., 8192, ...)` in `transcribe_task_fn` and `tts_say_task_fn` with PSRAM-backed static tasks (`xTaskCreateStatic` with `MALLOC_CAP_SPIRAM` stack + internal `StaticTask_t`) guarded by active-task flags so repeated tests never exhaust internal DRAM.
- Handle `"record_abort"` frame in `handle_frame()` to cleanly disarm manual recording and reset state.

#### [MODIFY] [tool_audio.h](file:///c:/repository/espet/main/tools/tool_audio.h) & [tool_audio.c](file:///c:/repository/espet/main/tools/tool_audio.c)
- Add `s_manual_owner` / `s_pcm_locked` state:
  - `tool_audio_manual_record_start()` checks `s_playing || s_speaker_busy || s_pcm_locked` (returning `ESP_ERR_INVALID_STATE` cleanly instead of recording 0 samples during TTS download), aborts any active VAD capture, and sets `s_manual_owner = true`.
  - `tool_audio_manual_record_stop()` keeps `s_pcm_locked = true` until `tool_audio_manual_capture_release()` is called after `voice_client_transcribe_full()` finishes reading `s_pcm_buf`.
  - Expose `bool tool_audio_is_manual_active(void)` so `voice_task` never calls `tool_audio_capture_disarm()` or arms a new VAD capture while manual recording or upload owns `s_pcm_buf`.
  - Add `void tool_audio_manual_record_abort(void)` to force-clear `s_recording`, `s_manual_owner`, and `s_pcm_locked`.

#### [MODIFY] [voice_client.c](file:///c:/repository/espet/main/voice/voice_client.c)
- In `voice_task()`, treat `tool_audio_is_manual_active()` as paused (same as `tool_audio_is_busy()`) so VAD never steals or disarms `s_pcm_buf` during manual `/stt` capture or upload.
- In `voice_client_transcribe_full()` and `voice_tts_fetch()`, only call `backend_discovery_invalidate()` after consecutive failures (e.g., 3 failures in a row) rather than wiping the working backend URL on a single transient timeout.

---

### Part 3: Tests & Documentation

#### [MODIFY] [test_voice_debug_bench.py](file:///c:/repository/espet/espet-backend/tests/test_voice_debug_bench.py)
- Add unit/integration tests for:
  - Atomic capture ring + `GET /api/voice/captures/{capture_id}` & `GET /api/voice/debug/last-result` (verifying `Cache-Control: no-store` and exact capture matching across sequential captures).
  - Concurrent/sequential `record/stop` + `/api/voice/transcribe` preserving the exact WAV per capture ID without `last_capture.wav` race corruption.
  - `POST /api/cloud/record/abort` future cleanup and clean error responses.
- Update `.agent/SYSTEM_OVERVIEW.md` per Session Memory Rule.

---

## Verification Plan

### Automated Tests
1. Backend & regression suite:
   ```powershell
   cd espet-backend
   python -m pytest tests/test_voice_debug_bench.py tests/test_tts.py tests/test_stt_speed.py tests/test_oled_summarizer.py tests/test_memory_system.py tests/test_rag_service.py -v
   python tests/test_cloud_gateway.py
   ```
2. Firmware compile verification (after Part 2 edits):
   ```powershell
   idf.py build
   ```

### Manual Verification on `/stt`
1. Run 10 consecutive **Start HW Mic Capture → Stop & Transcribe** cycles (with VAD both OFF and ON), varying spoken length (1s, 4s, 8s):
   - Confirm `<audio>` duration (`0:01`, `0:04`, `0:08`), waveform, and `DURATION / SAMPLES` match the spoken clip every time.
   - Confirm `"..."` quotes or long sentences in Whisper output never cause 20s timeouts.
2. Alternate **Speak on ESP32 Speaker** and **Start HW Mic Capture** 5+ times, plus **Transcribe File** (including invalid/empty file error recovery) 8+ times:
   - Confirm no `AudioContext` exhaustion, no `task_fail`, and no `no_audio` lockout.
3. Click **Run Regression Suite** and **Verify Production Contracts** on `/stt` to confirm 100% pass.
