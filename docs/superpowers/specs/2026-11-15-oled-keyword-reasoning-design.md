# Specification: OLED Key Word Reasoning & Display for Spoken Responses

- **Date**: 2026-11-15
- **Status**: Approved Design
- **Target Subsystems**: `espet-backend` (voice router & summarizer service), `main/` (voice client & display status)

---

## 1. Problem Statement
The ESPET device features a 0.96-inch 128×64 SSD1306 OLED display using an 8×16 monospace font, allowing at most 16 characters per line across 2 text lines (32 characters total).

When an agent replies to a user message (e.g. from Telegram, voice, or cloud) and reads out the response via Text-To-Speech (TTS), the firmware currently performs a crude substring slice (`text[0..15]` on line 1, `text[16..31]` on line 2), chopping words in half (e.g. `"living room"` becomes `"liv-"` and `"ing room"`) and discarding everything past 32 characters.

The goal is to maintain full, natural TTS audio playback while using an intelligent keyword extraction algorithm to display only the most important, punchy words on the OLED screen, with strict guarantees that no line exceeds 16 characters and words are never split mid-word.

---

## 2. Architecture & Data Flow

```
[Agent Response Text] (e.g. "I've turned on the status LED for you!")
         │
         ▼
[ESP32: outbound_dispatch_task / voice_client_speak]
         │
         │  HTTP POST /api/voice/tts {"text": "..."}
         ▼
[espet-backend: /api/voice/tts]
  ├─ 1. edge-tts synthesizes 16 kHz WAV audio (full sentence)
  ├─ 2. oled_summarizer extracts key words:
  │      - Line 1: "Status LED" (10 chars <= 16)
  │      - Line 2: "State: ON"   (9 chars <= 16)
  └─ 3. Response Headers:
         X-OLED-Line1: Status LED
         X-OLED-Line2: State: ON
         Body: binary audio/wav
         │
         ▼
[ESP32: voice_tts_fetch / rx_event_handler]
  ├─ Captures X-OLED-Line1 and X-OLED-Line2 from HTTP headers
  ├─ Displays key words: display_status_set(line1, line2)
  └─ Plays WAV audio through MAX98357A speaker
```

---

## 3. Detailed Component Specifications

### 3.1 Backend Service: `espet-backend/services/oled_summarizer.py`
A dedicated summarizer module providing `extract_oled_keywords(text: str) -> tuple[str, str]`:

1. **Text Normalization**:
   - Strips emojis and Unicode pictographs using regex.
   - Strips Markdown formatting (bold `**`, italics `*`, inline code `` ` ``, markdown headers).
   - Strips conversational conversational prefixes (`"Sure, "`, `"Okay, "`, `"I have "`, `"Here is "`, `"As requested, "`).

2. **Pattern Matching & Keyword Extraction**:
   - **Hardware / Device Controls**: Detects status/actions (e.g., LED on/off, GPIO pins, camera snaps, audio) and emits structured state (e.g. `L1: "White LED"`, `L2: "Status: ON"`).
   - **Weather / Environment**: Detects city, temperature, and conditions (e.g. `L1: "Kuala Lumpur"`, `L2: "31C, Rain"`).
   - **Calendar / Time / Tasks**: Detects dates, times, and events (e.g. `L1: "Meeting: Team"`, `L2: "Today 3:00 PM"`).
   - **General Fallback**: Removes English and common conversational stopwords (`the`, `is`, `a`, `to`, `for`, `of`, `in`, `and`), isolates primary subject and action nouns/verbs, and performs greedy word-wrapping:
     - Line 1: Accumulate words up to 16 characters.
     - Line 2: Accumulate next words up to 16 characters.
     - Never split a word across lines or truncate mid-word unless a single word itself exceeds 16 characters.

3. **Strict Invariant**:
   - `len(line1) <= 16`
   - `len(line2) <= 16`
   - Both strings stripped of leading/trailing whitespace.

### 3.2 Backend Endpoint: `espet-backend/api/voice_router.py`
In `@router.post("/tts")`:
- Computes `line1, line2 = extract_oled_keywords(req.text)`.
- Adds headers to `fastapi.responses.Response`:
  ```python
  headers = {
      "X-OLED-Line1": line1,
      "X-OLED-Line2": line2,
  }
  ```

### 3.3 ESP32 Firmware: `main/voice/voice_client.c`
1. **Header Parsing in `rx_event_handler`**:
   - Extend `rx_buf_t`:
     ```c
     typedef struct {
         uint8_t *buf;
         size_t len;
         size_t cap;
         bool oom;
         char oled_line1[17];
         char oled_line2[17];
     } rx_buf_t;
     ```
   - On `HTTP_EVENT_ON_HEADER`:
     - If key matches `"X-OLED-Line1"`, copy value into `rx->oled_line1` (max 16 chars + null terminator).
     - If key matches `"X-OLED-Line2"`, copy value into `rx->oled_line2` (max 16 chars + null terminator).

2. **Display Update in `voice_speak_internal`**:
   - Modify `voice_tts_fetch` to pass back the parsed `oled_line1` and `oled_line2` (or populate a struct).
   - In `voice_speak_internal`:
     - If explicit `display_l1` / `display_l2` were passed, use those.
     - Else if backend returned `oled_line1` / `oled_line2`, use those via `display_status_set(oled_line1, oled_line2)`.
     - Else fallback to local word-boundary wrapping of `text`.
   - Play audio via `tool_audio_play_wav`.
   - On completion, `voice_client_speak` clears the screen back to `display_status_idle()`.

---

## 4. Edge Cases & Error Handling

1. **Short Text ($\le 16$ characters)**:
   - Line 1 receives the entire text, Line 2 remains blank.
2. **Backend Down / Network Failure**:
   - If TTS fetch fails, error beep plays and firmware falls back to clean local word-wrapped error message (`"TTS failed"`, `"Check backend"`).
3. **Empty or Whitespace-only Response**:
   - Returns empty lines; OLED remains on idle face.
4. **Header Encoding**:
   - ASCII-clean header values (or URL-encoded if non-ASCII) to prevent HTTP protocol violations in standard web servers.

---

## 5. Verification Plan

1. **Backend Unit Tests (`espet-backend/tests/test_oled_summarizer.py`)**:
   - Test action statements (`"Turned on the white LED on GPIO 42"`).
   - Test weather statements (`"The weather today is cloudy with a high of 32 degrees"`).
   - Test long paragraphs (>100 characters).
   - Test markdown and emojis (`"Sure! 📸 *Snapshot* taken: \`image.jpg\`"`).
   - Assert `len(line1) <= 16` and `len(line2) <= 16` on 100% of test cases.
2. **Firmware Build Verification**:
   - Run `idf.py build` to ensure C syntax, PSRAM buffer safety, and header parsing compile cleanly without errors or warnings.
