# Herman Roadmap — Tools & Functions Needed for the Proactive Half

> Herman is the **proactive** half of ESPET: the embodied agent that initiates
> helpful actions without being asked (per `AGENTS V3.MD` SPLIT v2, summarized
> in `.agent/SYSTEM_OVERVIEW.md` §9). His research question: *how can an
> embodied agent initiate helpful actions and adapt without commands?*
> His metrics: **trigger precision/recall**, **false-interruption rate**,
> **memory retention**.
>
> This document is the complete inventory of tools/functions Herman needs that
> do not exist yet, with rationale for each and a placement decision: what must
> run on the ESP32-S3 (capture/actuate/deliver) vs the backend (compute/memory/
> integration). Verified against branch `herman-2` code as of 2026-09-08.

---

## 0. The constraint that shapes everything

The ESP32-S3 has ~40 KB of usable internal SRAM, 8 MB PSRAM, and no OS —
while the backend is an always-on LAN server with effectively unlimited RAM,
disk, and Python's entire ecosystem. Herman's triggers (cron, heartbeat,
buttons) all run on-device, but **anything beyond trivial string handling
belongs on the backend**:

| Concern | ESP32-S3 | Backend |
|---|---|---|
| Scheduling, timekeeping | ✅ trivial | — |
| Sensor capture (mic/camera/buttons) | ✅ must (hardware is here) | — |
| Speech-to-text, TTS, NLP | ❌ impossible | ✅ |
| LLM calls, agent loop | ⚠️ minimal (already there) | ✅ preferred for heavy work |
| Vision (VLM) analysis of camera frames | ❌ | ✅ |
| Memory storage/retrieval/consolidation | ⚠️ raw SPIFFS only | ✅ |
| Trigger logging + metrics | ⚠️ counters only | ✅ source of truth |

Design rule of thumb: **the device captures, decides-lite, and delivers; the
backend thinks.** Every item below follows this split.

---

## 1. Current-state audit (what Herman has today, and where it breaks)

All findings verified against `herman-2` source:

1. **Proactive triggers exist but can't be seen or heard.**
   - `cron_service.c` fires jobs by pushing a message to the inbound bus, but
     replies routed to channel `system` are only `ESP_LOGI`'d to serial
     (`mimi.c:148-149`) — no OLED, no speaker. A proactive assistant nobody
     can see or hear is invisible in a demo and unmeasurable in a study.
   - Heartbeat (`heartbeat.c`, every 30 min) reads `/spiffs/HEARTBEAT.md` —
     but that file is **not in `spiffs_data/`** and **not synced** by
     `config_sync.c`, so on a fresh flash the heartbeat loop never fires.

2. **No time source.** There is no SNTP anywhere in the firmware; the clock is
   only set when the agent voluntarily calls `get_current_time` (which reads
   the `Date` header from api.telegram.org). After every reboot, `at`-type
   cron jobs misfire or are skipped until the agent happens to fix the clock.

3. **No voice I/O at all.**
   - PTT (green button) records 16 kHz PCM into PSRAM and on release dumps
     RMS/min/max stats **to serial** (`tool_audio.c` PIPELINE-1..4 comments).
   - `audio_play_tone` is a stub; the I2S TX channel is never created, so the
     MAX98357A amp is silent.
   - There is no `/api/transcribe` or `/api/tts` endpoint on the backend.

4. **Scheduling is interval/one-shot only.** `cron_job_t` supports `every`
   (interval seconds) and `at` (epoch). No "daily at 08:00", no day-of-week,
   no quiet hours. Herman can't say "brief me every morning at 8" — the most
   basic proactive behavior.

5. **Memory is write-only-ish and tiny.** The agent can `write_file`
   MEMORY.md, but there is no append tool, no consolidation pass, and recall
   is hard-capped at 4 KB MEMORY.md + 3 days × 4 KB of daily notes. Nothing
   measures retention.

6. **Camera bypasses the brain.** White-button snapshot goes straight to
   Telegram with a canned caption; the agent (and any VLM) never sees it.
   Video mode is a frame-dropping mock. Vision is only on the `jeff` branch.

7. **Backend cannot push.** All backend→device flow is pull (config sync,
   minutes-scale). A real event ("email arrived from your professor") can't
   reach Herman until his next sync tick.

---

## 2. P0 — Make the proactive loop observable and correct

These are prerequisites for everything else; without them Herman cannot
demonstrate *any* proactive behavior to a user or an examiner.

### 2.1 `system`-channel delivery (OLED + voice on device) — **C firmware**

- **Gap:** outbound dispatch for `channel="system"` is `ESP_LOGI` only
  (`main/mimi.c:148-149`). Every cron/heartbeat reply currently dies in the
  serial log.
- **Function:** `deliver_system(const char *text)` in the outbound dispatcher —
  chunk to ~32 chars/page, push each page through `tool_oled_print`, hold each
  page ~3 s (or until next message), blink the white LED while a proactive
  message is pending.
- **Why on-device:** the OLED and LED are on-device; this is pure delivery.
  Cheap (no new hardware paths) and instantly makes all existing cron jobs
  visible.
- **Effort:** S. **Depends on:** nothing.

### 2.2 SNTP time sync — **C firmware** (esp_netif/sntp lwip component)

- **Gap:** no SNTP anywhere; clock = wrong until an agent turn calls
  `get_current_time`. All `at` scheduling is untrustworthy after boot.
- **Function:** call `esp_sntp_init()` in `wifi_manager` after connection with
  MY pool servers (e.g. `pool.ntp.org`, `time.google.com`); set TZ first from
  config sync (already applied via `setenv("TZ")`).
- **Why on-device:** timekeeping is an OS service; it's a built-in lwip
  component (zero flash cost beyond KBs) and removes a whole class of
  flaky-schedule bugs that would poison trigger-precision measurements.
- **Effort:** S. **Depends on:** nothing. Highest value/effort ratio on this
  list.

### 2.3 Activate HEARTBEAT.md — **seed + sync section** (C + Python)

- **Gap:** `heartbeat.c` gates on `/spiffs/HEARTBEAT.md`, which is neither
  flashed (`spiffs_data/` lacks it) nor synced (5-section enum in
  `config_sync.c`). The heartbeat never fires on real hardware.
- **Function:** (a) ship `spiffs_data/HEARTBEAT.md` seed; (b) new `heartbeat`
  config section: backend stores `heartbeat_md`, device writes the file on
  version bump — exactly the `soul_md` pattern; (c) dashboard editor card on
  the Device view (or its own view).
- **Why:** heartbeat is Herman's *adaptive* trigger — the mechanism that lets
  him decide on his own what deserves attention. It's 80% built and inert;
  activation is nearly free. It becomes Herman's testbed for
  false-interruption metrics (the `heartbeat_has_tasks()` gate is literally a
  false-interruption filter — measure it).
- **Effort:** S–M. **Depends on:** nothing.

### 2.4 Time-of-day cron (`daily HH:MM`) — **C firmware** (`cron_service.c`) + backend mirror

- **Gap:** only `every`/`at` exist. "Brief me every morning at 8" cannot be
  expressed; users must compute interval hacks that drift.
- **Function:** extend `cron_job_t` with `kind="daily"` + `time_of_day`
  (minutes since midnight, device-local TZ already applied by config sync).
  `cron_process_due_jobs` compares current HH:MM; `cron_add` tool schema gets
  the new kind; backend `config_store.py` validation + dashboard Schedule
  modal get a "Daily at" segment option.
- **Why on-device:** cron evaluation is 20 lines of C on data already in RAM;
  offloading it would add a network round-trip dependency to the most
  reliability-critical path. This is the single most user-visible scheduling
  upgrade.
- **Effort:** M. **Depends on:** 2.2 (SNTP) for trustworthy local time.

---

## 3. P1 — Core proactive capabilities (the actual demo material)

### 3.1 TTS: backend synthesis + device playback — **backend `/api/tts` + C I2S TX**

- **Gap:** speaker amp wired (GPIO 2, shares BCLK/WS 41/42) but I2S TX is
  never initialized; `audio_play_tone` is a stub; no backend TTS endpoint.
  Herman is mute.
- **Function:**
  - Backend: `POST /api/tts {text, voice}` → WAV (16 kHz mono 16-bit) via
    edge-tts / pyttsx3 / any synthesize (pluggable `tts_provider.py`);
    returns audio bytes. Optionally cache by hash(text+voice) on disk.
  - Device: create I2S TX channel in `tool_audio.c`; new tool
    `audio_speak {text}` → `POST /api/tts` → stream WAV → I2S TX (chunked,
    PSRAM ring buffer); plus fix `audio_play_tone` to a real DMA sine wave.
  - Agent plumbing: after producing a reply on channel `system`, the outbound
    dispatcher plays TTS (2.1's voice half).
- **Why split:** TTS inference is far beyond S3 capability; playback is a
  pure hardware path only the device can do. This one feature converts every
  proactive message from text-on-log to an actual embodied utterance — the
  "embodied" in embodied agent.
- **Effort:** M (backend) + M (device). **Depends on:** 2.1 for delivery path.
  *Pairs with Jeff's branch:* needs I2S bus arbitration with RX if both active.

### 3.2 STT: PTT → transcript → agent loop — **backend `/api/transcribe` + C pipeline**

- **Gap:** green button records PCM and dumps stats to serial (PIPELINE-1..4
  TODOs in `tool_audio.c`). Voice — the primary interface of an embodied
  assistant — doesn't reach the brain.
- **Function:**
  - Backend: `POST /api/transcribe` accepting raw PCM (16 kHz/16-bit/mono +
    query params) or WAV; provider-pluggable (`stt_provider.py`: faster-
    whisper locally, OpenAI/Google cloud as alternates); returns
    `{transcript, language, duration_ms}`.
  - Device: on PTT release, `POST` the PCM buffer (chunked upload over LAN,
    ~1.9 MB for 30 s ≈ 1 s on 100 Mbps LAN); receive transcript; push
    `mimi_msg_t{channel: source}` onto the inbound bus (PIPELINE-2).
- **Why split:** ASR is impossible on-S3; transfer of raw PCM over LAN is
  trivial and keeps the device a dumb capture endpoint (zero format
  conversion needed — mic is already 16/16/mono).
- **Effort:** M + M. **Depends on:** nothing (independent of TTS).
- **Herman tie-in:** with STT, cron/heartbeat replies can also be *voiced*,
  and voice becomes a trigger channel to measure.

### 3.3 Camera → agent (snapshot path through the LLM) — **C firmware** (+ merge Jeff's vision work)

- **Gap:** white-button snapshot bypasses the agent entirely
  (`mimi.c:41-57` sends photo to Telegram with canned caption). Vision
  injection (`llm_proxy_set_pending_image`) exists only on the `jeff` branch
  (commit `d8d1a59`), not in `herman-2`. Video is a frame-dropping mock
  (`tool_camera.c:98-120`).
- **Function:**
  - Merge/rebase `jeff`'s one-shot image injection into `herman-2`.
  - White button: snapshot → **agent loop** prompt ("Describe what you see
    and anything unusual") with image attached → reply to last chat + OLED.
  - New local tool `camera_snapshot {}` returning base64 (PSRAM-backed) so
    *the agent itself* can choose to look — Herman deciding to look is
    proactive behavior; a button deciding for him is not.
  - Drop or descope video (honest mock removal) until needed.
- **Why:** a proactive embodiment that perceives its environment (is the user
  at the desk? is the light on?) is the difference between a scheduler and an
  agent. Also the trigger source for scene-watch (3.6).
- **Effort:** M. **Depends on:** Jeff's vision branch (shared L0).

### 3.4 `memory_append` tool + memory consolidation daemon — **C tool + backend job**

- **Gap:** no append tool (agent must read-modify-write 4 KB through
  `write_file`); no consolidation; recall capped at MEMORY.md + 3 days of
  notes; retention — a core Herman metric — is unmanaged and unmeasured.
- **Function:**
  - Device tool `memory_append {fact}` → `memory_append_today()` (exists in
    `memory_store.c`, currently uncalled by any tool!) appends a dated bullet
    to today's daily note. Trivial, safe, immediately useful.
  - Backend "nightly consolidation" cron (dashboard Schedule job kind or
    backend-internal): fetches the day's notes (new lightweight endpoint or
    piggyback on config bundle), asks the LLM to distill durable facts, and
    writes back MEMORY.md via the existing version-bumped store. Device pulls
    the merged memory next sync — the existing "web wins once" flow already
    handles delivery.
- **Why split:** LLM summarization can't run on-device; the device part is
  one registered tool wrapping an existing function.
- **Herman tie-in:** this IS the memory-retention metric: measure recall of
  facts injected days earlier, with vs without consolidation.

### 3.5 Quiet hours / DND for proactive delivery — **C firmware + config**

- **Gap:** nothing prevents a 3 a.m. "daily briefing" from lighting up the
  OLED and (once TTS lands) talking. False-interruption rate is a core
  Herman metric with no mechanism behind it.
- **Function:** `device` config section gains `quiet_hours: {start:"23:00",
  end:"08:00"}`; `cron_process_due_jobs` (and heartbeat) skip delivery during
  the window (queue or drop per job flag `respect_quiet`), logging skipped
  runs for metrics.
- **Why on-device:** delivery gating is a device concern (the device knows
  its TZ and its moment of firing). Trivial C.
- **Depends on:** 2.2. **Effort:** S.

### 3.6 Mic-RMS and GPIO event triggers — **C firmware (capture task hook)**

- **Gap:** sensing is buttons-only. The INMP441 already streams into a DMA
  buffer (`capture_task`, Core 1) but nothing observes loudness. There's no
  "knock/knock-sensor" style trigger to the inbound bus.
- **Function:** in the capture path, compute a cheap block RMS; if above
  threshold with hysteresis + cooldown (e.g. ≥N blocks above -30 dBFS, then
  60 s cooldown), push `{"source":"sound"}` message to the inbound bus so the
  agent decides what it was (with STT from 3.2, this becomes "clap → what's
  up?"). Same pattern for any spare GPIO edge wired to a sensor.
- **Why on-device:** only the device touches I2S/GPIO; the threshold check is
  a handful of multiplies. The *decision* of what to do still goes through
  the agent (LLM), keeping intelligence off-chip.
- **Herman tie-in:** this is an *event trigger* class — directly grows
  trigger recall; threshold tuning is where precision/recall trade-offs get
  measured. **Effort:** M. **Depends on:** 3.2 to be useful (otherwise the
  agent can't hear what caused the trigger).

---

## 4. P2 — Research-grade infrastructure

### 4.1 Backend→device push channel — **backend + C WS client**

- **Gap:** everything device-bound is pulled (config sync, default 5 min).
  Latency-sensitive events (notify now, scene alarm, transcribe result is
  fine as HTTP response) can't reach Herman in seconds.
- **Function:** reuse the existing ws_server pattern inverted: a lightweight
  device WS *client* to the backend (`/ws/device`, same bearer key), backend
  pushes `{type:"event", ...}` frames; device funnels them onto the inbound
  bus. Fallback: device long-polls `GET /api/events?since=...` every 30 s
  (simpler, no idle socket on ESP32).
- **Why:** unlocks "email arrived → Herman speaks within seconds" class
  behaviors; also the transport for trigger telemetry (4.2).
- **Effort:** M–L. **Depends on:** nothing, but only worth it after P1.

### 4.2 Trigger logging + precision/recall instrumentation — **backend (SQLite) + C counters**

- **Gap:** Herman's three metrics (trigger precision/recall, false-
  interruption rate, memory retention) have zero instrumentation. The
  heartbeat gate (`heartbeat_has_tasks()`) and cron firing are unlogged
  beyond serial.
- **Function:**
  - Device: append one JSONL line per proactive fire to
    `/spiffs/telemetry.jsonl` (ring-buffered, uploaded on sync pull ACK —
    or via 4.1). Fields: trigger_type, job_id/heartbeat, ts, delivered, ack.
  - Backend: `POST /api/telemetry` → SQLite (`db/telemetry.db`); dashboard
    card: fires/day, delivered, user-response rate; user marks "wanted /
    didn't want" (or infers from reply) → precision/recall and interruption
    rate over time.
- **Why split:** analysis/storage obviously backend; the device only emits
  events. Without this, the FYP report has anecdotes, not data.
- **Effort:** M. **Depends on:** ideally 4.1; a batch upload on sync is fine
  for a start.

### 4.3 Scene-watch jobs (camera + cron) — **backend analysis + device capture**

- **Gap:** no combination of periodic capture + analysis. Herman can't say
  "check the desk every hour and tell me if it's messy / lights left on /
  plant drooping".
- **Function:** cron job kind gets `attach:"camera"`; on fire, device captures
  a JPEG, POSTs it to `POST /api/vision/analyze {prompt}` (backend calls a
  VLM — Claude/GPT-4o-mini/LM Studio multimodal), and the agent gets the
  structured result back to decide whether the user should be alerted.
- **Why split:** VLM inference is backend-only; capture is device-only.
- **Depends on:** 3.3 (camera→agent plumbing), 2.4 (daily scheduling).
  **Effort:** M.

### 4.4 Memory retrieval tuning (relevance-window selection) — **backend-assisted prompt assembly**

- **Gap:** context builder includes fixed windows (4 KB MEMORY.md + last 3
  days). Proactive prompts (heartbeat) may need facts older than 3 days;
  reactive prompts don't need everything.
- **Function:** backend keyword/embedding index over memory + notes (built
  during 3.4 consolidation); new endpoint `POST /api/memory/query {query,k}`
  returns top-k snippets; device `context_builder` calls it when building
  heartbeat/cron prompts (with cache + graceful fallback to the static
  window on timeout).
- **Why split:** embeddings/keyword search on 8 MB PSRAM against flash files
  is possible but painful in C; on the backend it's 30 lines and improves
  with 3.4's consolidation loop.
- **Depends on:** 3.4. **Effort:** M. **Herman tie-in:** retention metric's
  final lever — measure recall with static-window vs relevance retrieval.

---

## 5. Suggested order

```
Wave 1 (P0, device-only, no backend changes):
  2.2 SNTP            → schedules become trustworthy
  2.1 system delivery → proactive output becomes visible
  2.3 HEARTBEAT.md    → adaptive loop activates
  3.5 quiet hours     → false-interruption control (before demoing TTS!)

Wave 2 (P1 audio, backend+device):
  3.1 TTS             → Herman speaks
  3.2 STT             → Herman hears (voice loop complete)
  3.4 memory_append   → agent-side memory hygiene (tiny device change)

Wave 3 (P1 deeper):
  2.4 daily cron      → natural-language schedulable routines
  3.3 camera→agent    → perception (sync jeff branch first)
  3.6 event triggers  → sound/pin sensing

Wave 4 (P2 research infra):
  4.2 telemetry       → start collecting metrics ASAP, it needs runtime
  4.1 push channel    → realtime events
  4.3 scene-watch     → showcase combo feature
  4.4 retrieval       → retention optimization
```

Rationale: observability before capability (a silent, mis-timed Herman
invalidates every later measurement); audio before camera (voice is the
primary embodiment channel and 3.1/3.2 are independent of Jeff's branch);
telemetry early in wave 4 so every subsequent feature generates measurable
data.

---

## 6. Item summary table

| # | Item | Lives on | Priority | Effort | Depends on |
|---|---|---|---|---|---|
| 2.1 | system-channel OLED/LED delivery | ESP32 C | P0 | S | — |
| 2.2 | SNTP time sync | ESP32 C | P0 | S | — |
| 2.3 | HEARTBEAT.md seed + sync section | both | P0 | S–M | — |
| 2.4 | daily HH:MM cron kind | ESP32 C + backend | P0 | M | 2.2 |
| 3.1 | TTS (`/api/tts` + I2S TX) | backend + ESP32 | P1 | M+M | 2.1 |
| 3.2 | STT (`/api/transcribe` + PTT upload) | backend + ESP32 | P1 | M+M | — |
| 3.3 | camera→agent snapshot path | ESP32 C (+ jeff merge) | P1 | M | jeff vision |
| 3.4 | memory_append + nightly consolidation | ESP32 tool + backend | P1 | S+M | — |
| 3.5 | quiet hours / DND | ESP32 C + config | P1 | S | 2.2 |
| 3.6 | mic-RMS / GPIO event triggers | ESP32 C | P1 | M | 3.2 useful |
| 4.1 | backend→device push (WS client or long-poll) | backend + ESP32 | P2 | M–L | — |
| 4.2 | trigger telemetry + precision/recall | ESP32 counters + backend | P2 | M | 4.1 ideal |
| 4.3 | scene-watch jobs (camera+cron+VLM) | both | P2 | M | 3.3, 2.4 |
| 4.4 | relevance-based memory retrieval | backend + ESP32 hook | P2 | M | 3.4 |

Every item keeps the invariant: **the ESP32 captures, actuates, and delivers;
the backend thinks.** Nothing proposed here asks the S3 to run ASR, TTS, VLM,
embedding, or summarization workloads.
