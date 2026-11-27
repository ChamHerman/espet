# ESPET — Full System Overview

> **ESPET** (Embodied Sensing & Personal Execution Terminal) is an embodied AI agent built on an
> **ESP32-S3** microcontroller. It listens, speaks, shows, and — most importantly — **acts**: it runs a
> ReAct agent loop on-device, calls local hardware tools directly, and reaches external services
> (Gmail, Google Calendar, Google Drive) through a customized MCP backend.
>
> The on-device agent is called **MimiClaw**.
>
> | | |
> |---|---|
> | **Firmware** | Pure C, ESP-IDF v5.5+, FreeRTOS (`main/`) |
> | **Backend** | Python 3.11+, FastAPI (`espet-backend/`) |
> | **Dashboard** | Vanilla HTML/CSS/JS + anime.js (`espet-backend/static/`) |
> | **LLM** | OpenAI-compatible endpoint (LM Studio / cloud) or Anthropic API |
> | **Docs** | This file · `docs/ARCHITECTURE.md` (firmware detail) · `.agent/ESPET_PINS.MD` (wiring) |

---

## 1. Big Picture

```
        YOU
        │  "Hi ESP" wake word · camera button · Telegram / Feishu / dashboard
        ▼
┌────────────────────────────────────────────────────────────────────┐
│                     ESP32-S3 DEVICE  (MimiClaw)                    │
│                                                                    │
│  ┌──────────── INPUT ────────────┐   ┌──────── OUTPUT ─────────┐  │
│  │ INMP441 I²S microphone        │   │ MAX98357A I²S speaker    │  │
│  │ Green button (PTT / trigger)  │   │ SSD1306 OLED Expressive  │  │
│  │ White button (camera / PT)    │   │ Eyes · Telegram / Feishu │  │
│  │ Telegram long-poll + Feishu   │   └──────────────────────────┘  │
│  └───────────────┬───────────────┘                                 │
│                  │ inbound message                                 │
│                  ▼                                                 │
│        ┌──────────────────┐      ┌──────────────────────────┐      │
│        │   Message Bus    │─────▶│  AGENT LOOP (Core 1)     │      │
│        │  (FreeRTOS q)    │      │  ReAct, max 10 iters     │      │
│        └──────────────────┘      │                          │      │
│                                  │  1. Build context        │      │
│  ┌──────────── DAEMONS ───────┐  │     (SOUL+USER+MEMORY+   │      │
│  │ cron_service  — schedules  │─▶│      skills + history)   │      │
│  │ heartbeat     — periodic   │  │  2. Call LLM             │      │
│  │ config_sync   — web config │  │  3. tool_use? → dispatch │      │
│  └────────────────────────────┘  │  4. Feed result back     │      │
│                                  │  5. Repeat until done    │      │
│  ┌──────────── TOOLS ─────────┐  └────────────┬─────────────┘      │
│  │ LOCAL  : gpio, audio,      │               │                    │
│  │ camera, files, cron, time, │               │ remote tool        │
│  │ web_search                 │               │                    │
│  │ REMOTE : gmail / calendar /│               │                    │
│  │ docs / drive  ─────────────┼───────┐       │                    │
│  └────────────────────────────┘       │       │                    │
│                                       ▼       ▼                    │
│  SPIFFS: SOUL.md · USER.md · MEMORY.md · cron.json · sync_state   │
└───────────────────────────────────────┼───────────────────────────┘
                                        │ WiFi · HTTP + Bearer device key
                                        ▼
┌────────────────────────────────────────────────────────────────────┐
│                     ESPET BACKEND  (FastAPI :8000)                 │
│                                                                    │
│  POST /api/mcp/execute ── single generic tool endpoint             │
│        │  auto-routes by tool name (no if/else anywhere)           │
│        ▼                                                           │
│  MCP Client ──▶ Google remote MCP servers (Gmail/Cal/Docs/Drive)   │
│  OAuth Manager ──▶ encrypted token store (SQLite, auto-refresh)    │
│  Config Store ──▶ data/config.json (profile/personality/memory/    │
│                    device settings/web cron jobs, per-section      │
│                    versions — this is what the device pulls)       │
│  Web Dashboard ──▶ GET /  (control center UI)                      │
└────────────────────────────────────────────────────────────────────┘
```

The split is deliberate: **the ESP32 never holds OAuth tokens and never runs Python**; the
**backend never talks to hardware**. Everything crosses exactly one authenticated HTTP boundary.

---

## Getting Started — Run It

### Backend server (FastAPI, serves the dashboard)

```bash
cd espet-backend

# First time only:
python3 -m venv venv
source venv/bin/activate          # macOS/Linux  ·  Windows: venv\Scripts\activate
pip install -r requirements.txt
cp .env.example .env              # edit .env with your Google OAuth client ID / secret
                                  # and set DEVICE_API_KEY to match the firmware

# Every time:
npm run dev:win                   # Windows: uses venv\Scripts\python.exe + uvicorn --reload
npm run dev:mac                   # macOS: activates venv, python main.py
python main.py                    # or activate venv first and run directly

The server auto-seeds `data/config.json` with the default SOUL.md, USER.md and
MEMORY.md (mirrors `spiffs_data/`). The dashboard is just [http://localhost:8000](http://localhost:8000).

### One-time Google OAuth

Open the dashboard → **Overview** → click **Login with Google** (or **Re-link
Google** if you've linked before). The consent screen opens in a **popup**, so
the dashboard never navigates away. When you finish, the page detects the new
token, reconnects the MCP servers automatically, and the Overview card flips to
**Connected**. (If popups are blocked, open `http://localhost:8000/auth/google`
directly instead.) Tokens are stored in `db/tokens.db` and refreshed
automatically forever.

> **If the card says "Linked" but the MCP servers say "Auth required":** your
> stored refresh token is dead (`invalid_grant` on refresh). Just click
> **Re-link Google** and complete the consent again — the backend reconnects
> with the fresh token, no restart needed.

### Flash the firmware

```bash
# 1. Copy the secrets template and fill in your WiFi + backend IP
cp main/mimi_secrets.h.example main/mimi_secrets.h
#     → BACKEND_IP = the LAN IP of the machine running the backend
#     → MIMI_SECRET_WIFI_SSID / PASS

# 2. Build & flash
idf.py build flash monitor
```

The device connects to WiFi, then pulls `GET /api/config` from the backend on
every boot and every `sync_interval_s` seconds (default 5 min, configurable from
the Device page). Any edit you make in the dashboard appears on the device after
the next sync tick.

### Rebuild the dashboard / vendor assets

```bash
cd espet-backend
npm install --legacy-peer-deps   # lobe-charts peer-wants @lobehub/ui ^4; 5.x is fine at runtime
# install-scripts approve esbuild + @lobehub/editor (postinstall) if npm blocks them

npm run build:app       # React SPA: src/ → static/vendor/app/ (also stamps cache-bust ?v in index.html)
npm run build:vendor    # 3D robot viewer: tools/toy-entry.js → static/vendor/toy.bundle.js
npm run build           # both
```

Built assets are committed to `static/` (like `toy.bundle.js`), so the firmware
CI never needs Node. Rebuild after any change under `src/` (dashboard) or
`tools/toy-entry.js` (3D). `--legacy-peer-deps` is required once because
`@lobehub/charts` declares a `@lobehub/ui@^4` peer while the app ships 5.x.


### Ports & services quick reference

| Port | Service | Notes |
|------|---------|-------|
| `:8000` | FastAPI backend | Dashboard + API + MCP router |
| `:80` | WiFi onboarding (captive portal) | On the ESP32, first-boot only |
| `:1234` | LM Studio (optional) | LLM inference if using local model |
| `:18789` | WebSocket gateway | On the ESP32, LAN debug clients |
| `:18790` | Feishu webhook receiver | On the ESP32 |

---

## 2. Hardware Platform

Shared board: **ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM), partitioned by `partitions.csv`:
two 3 MB app slots, 7.8 MB SPIFFS (agent files), 2 MB `model` partition (WakeNet
wake-word models, esp-sr), 64 KB coredump.

| Peripheral | Chip / Part | Pins | Notes |
|---|---|---|---|
| Microphone | INMP441 (I²S) | SD 40, SCK 38, WS 39 | L/R tied to GND; always-on WakeNet listener ("Hi ESP") |
| Speaker amp | MAX98357A (I²S) | DIN 21, BCLK 38, LRC 39 | Shares I²S bus with the mic — full-duplex TX+RX pair; tones + TTS WAV playback (§7.1 voice flow) |
| Camera button | Push (input pull-up) | 41 | Short press = snapshot → Telegram, hold ≥600 ms = video |
| White button | Push (input pull-up) | *Unassigned* | Disabled / unassigned |
| White LED | — | 42 | Status / feedback |
| Camera | OV2640 | (esp32-camera driver) | Snapshot + video for VLM questions |

Full wiring reference: [`.agent/ESPET_PINS.MD`](./.agent/ESPET_PINS.MD), photo `esp32-pins.jpg`.

---

## 3. Firmware (`main/`)

### 3.1 Boot sequence (`mimi.c`)

```
nvs_flash → SPIFFS → message_bus → memory_store → skill_loader → session_mgr
→ wifi_manager → http_proxy → telegram_bot → feishu_bot → llm_proxy
→ tool_registry → camera → button_handler → cron_service → heartbeat
→ config_sync → agent_loop → serial_cli
│
wifi start ──fail──▶ BLE Wi-Fi onboarding (PROV_ESPET_XXXX, Web Bluetooth/app, reboots on success)
│ok
outbound dispatch task → agent_loop_start → backend_discovery_start (last)
→ telegram/feishu start → cron_start → heartbeat_start → ws_server → config_sync_start
```

### 3.2 The agent loop (`agent/agent_loop.c`) — ReAct pattern

1. **Context build** (`context_builder.c`): system prompt = `SOUL.md` (personality) +
   `USER.md` (profile) + `MEMORY.md` (long-term memory) + recent daily notes + loaded
   skill files + the tool list (local registry + remote schemas from `GET /api/tools`)
   + recent conversation history (`session_mgr.c`).
2. **LLM call** (`llm_proxy.c`): streams from the configured provider (cloud OpenAI-compatible
   such as `apihub.agnes-ai.com`, local LM Studio via `HOST_IP:1234`, or Anthropic). Optionally emits a working
   status to the caller's channel.
3. **Dispatch**: the LLM answers with either `text` (→ outbound queue → speaker/
   chat) or `tool_use`. The main model produces a single-pass `[OLED: Line 1 | Line 2]` header
   at the start of final conversational text responses; `agent_loop.c` parses and extracts this tag immediately
   upon LLM completion on Core 1, displaying it on the SSD1306 OLED in <5ms with an active readability hold
   (speech duration + 5s buffer, min 8s total), strips the tag in-place for clean session history storage
   and outbound delivery, and pushes to `s_outbound_queue`. Outbound dispatcher transmits voice
   immediately without blocking on Telegram network roundtrips. Each tool_use goes to the **tool registry**:
   - tool exists locally → executed on-chip (`tool_gpio`, `tool_audio`,
     `tool_camera` (`take_photo`), `get_device_status`, `restart_device`, `tool_files`, `tool_cron`, `tool_get_time`, `tool_web_search`)
   - otherwise → `POST /api/mcp/execute` to the backend (`tool_registry.c`)
4. **Loop**: the tool result is fed back as a new LLM message. Max 10 iterations /
   4 tool calls per request. Safety: GPIO writes go through `gpio_policy.c`.

### 3.3 Memory system (`memory/` & `espet-backend/services/`)

ESPET employs a hierarchical 3-tier memory lifecycle balancing device flash limits against long-term semantic persistence:

1. **Tier 1: On-Device Hot Working Scratchpad (`/spiffs/memory/MEMORY.md`)**:
   - Strictly bounded (< 3 KB) Markdown file stored on SPIFFS flash.
   - Read directly into the Core 1 agent loop system prompt on every turn (fast, offline-capable, zero network latency).
   - Contains high-priority user profile bullets, enduring preferences, and active goals.
   - Refreshed from the backend via automated distillation (`distill_working_memory`).
   - Session history is ring-buffered per chat in `/spiffs/sessions/tg_<chat_id>.jsonl` (last 20 messages).

2. **Tier 2: Lightweight Execution Action Audit Log (`action_logs` table)**:
   - Dedicated SQLite table storing raw tool invocations (`gpio_write`, `read_file`, `take_photo`, etc.) with 7-day rolling TTL (`purge_expired_actions`).
   - **Completely decoupled from vector RAG**: raw tool execution traces never enter `vec_memories`, preventing vector database bloat and semantic pollution.
   - Only significant external real-world state mutations (e.g. emails sent, calendar events created) are promoted to semantic event memory.

3. **Tier 3: Deep Semantic Vector Memory (SQLite-Vec `vec_memories`)**:
   - Uses `fastembed` (BAAI/bge-small-en-v1.5, 384 dimensions) with sqlite-vec cosine distance.
   - **3-Factor Salience Rubric**: Autonomous background extraction (`MemoryOrganizer`) evaluates turns against Durability (>30 days), Actionability, and User Relevance before saving.
   - **Conflict Resolution & Upsert**: Automatically detects conflicting or updated preferences (cosine distance <= 0.18) and updates existing chunks in-place rather than accumulating duplicate contradictions.
   - **Context-Aware Retrieval & Attention Priming**: Resolves pronouns/entities from recent turns before vector searching and formats retrieved memories into a prominent `## Established User Context (Priority: Follow Strictly)` banner directly above the active turn.
   - **Agent Memory Tools**: Exposed via MCP / custom tools: `memory_save`, `memory_search`, `memory_update`, and `memory_forget`.

| File / Store | Written by | Read by | Purpose |
|---|---|---|---|
| `/spiffs/config/SOUL.md` | web dashboard (config sync) | context builder | Personality, values, hard behavior rules |
| `/spiffs/config/USER.md` | web dashboard / MemoryOrganizer | context builder | Who the user is, language, timezone, context |
| `/spiffs/memory/MEMORY.md` | backend distillation / serial CLI | context builder | Bounded working memory scratchpad (< 3 KB) |
| `/spiffs/sessions/*` | session_mgr | agent loop | Rolling conversation history (20 msgs) |
| `action_logs` (backend DB) | cloud_router tool interceptor | audit / diagnostics | Ephemeral execution traces (7-day TTL) |
| `vec_memories` (backend DB) | MemoryOrganizer / agent tools | RAG injection / agent tools | Deep semantic vector memory with embeddings |

### 3.4 Proactive daemons

- **`cron/cron_service.c`** — up to 16 jobs in `/spiffs/cron.json`. Two kinds:
  `every` (interval seconds) and `at` (one-shot epoch, optionally
  `delete_after_run`). When a job fires it pushes a normal message onto the
  inbound bus with a destination `channel` (`system` = voice, or
  `telegram` + `chat_id`), so the agent treats it exactly like a user message
  — and can reply wherever the job says.
  Execution safety & lifecycle:
  - Clock uncalibrated guard: skips `at` jobs when `time(NULL) < 1700000000` (year 1970 boot state).
  - 15-minute staleness gate: one-shot jobs overdue by >900s are dropped and deleted/deactivated without triggering agent/alerts.
  - Sync report dispatch: changes immediately trigger `cloud_link_send_sync_report()` so backend deactivates expired/fired jobs and flips dashboard toggles OFF.
  Job schema (device ↔ web, identical):
  ```json
  {
    "id": "8830acf0", "name": "Morning briefing", "enabled": true,
    "kind": "every" | "at",
    "interval_s": 86400,          // for "every"
    "at_epoch": 1789088400,       // for "at"
    "message": "Give me a summary of my day",
    "channel": "system" | "telegram",
    "chat_id": "cron",
    "delete_after_run": false,
    "last_run": 0, "next_run": 0
  }
  ```
- **`heartbeat/heartbeat.c`** — every 30 min injects a heartbeat prompt so the
  agent can check for due context and act unprompted (Herman's proactive loop).
  Dispatched via `cron_service.c` (`cron_task_main`, 4KB stack) instead of FreeRTOS
  software timer to protect `Tmr Svc` daemon stack from SPIFFS VFS I/O overflow.
- **`sync/backend_discovery.c`** — runtime backend discovery. After Wi-Fi
  connects (and on every config-sync cycle) it queries mDNS for
  `_espet._tcp.local.` (advertised by the FastAPI backend via python-zeroconf)
  and caches the base URL `http://<ip>:<port>`. Because PTR/SRV and A records
  can arrive in separate mDNS response packets, it follows up with
  `mdns_query_a("<instance>")` (bare hostname, no `.local`) to fetch the IPv4
  address. Discovery starts only after all boot services are up — the
  internal-RAM mDNS task starved boot-time allocations (cfg_sync task creation
  failure, i2c/OLED assert crash) when started mid-boot. All backend
  callers (`tool_registry.c`, `config_sync.c`, `voice_client.c`) build URLs
  via `backend_discovery_base_url()`, which falls back to the compile-time
  `MIMI_BACKEND_URL` until discovery succeeds — so a changing laptop IP no
  longer requires a rebuild.
- **`sync/config_sync.c`** — web-config pull (see §5). Also syncs **skill
  files**: when the `skills` config version advances, the device pulls
  `GET /api/skills/sync` (separate endpoint — the bundle is far larger than
  the 32 KB config buffer), writes each enabled skill to `/spiffs/skills/`,
  and deletes previously-synced files that disappeared (disabled or removed
  on the web). Skills flashed at build time or created by the agent via
  `write_file` are never touched (filename-based ownership tracking in
  `sync_state.json`, mirroring the cron `synced_ids` contract).

### 3.5 Channels (`channels/`)

- **Telegram** (`telegram_bot.c`) — long-polling inbound, outbound text + photos
  (`telegram_bot_photo.c`). Bot token stored in NVS (`telegram_set_token`),
  set once via serial CLI. Replies go to the originating chat. (Note: when
  hybrid cloud link is active, polling is offloaded to backend gateway).
- **Cloud Gateway Link** (`channels/cloud_link/cloud_link.c`) — persistent
  outbound WebSocket connection (`/api/cloud/ws/device`) to the backend.
  Transports inbound Telegram updates (`message_in`), outbound replies
  (`message_out`, `photo_out`), and tunnels LLM inference requests
  (`llm.request` / `llm.response`) for keyless device execution. It also
  receives `camera_stream_start` / `camera_stream_stop` control frames that
  start the OV2640 live-stream task on demand, plus `tts_say` frames for
  backend-triggered local speech such as owner welcome alerts. `tts_say` can
  carry explicit OLED `display_title` / `display_body` lines so speech text and
  display text do not have to be identical.
- **Feishu / Lark** (`feishu_bot.c`) — archived channel integration, disabled
  by default via Kconfig toggle (`CONFIG_ESPET_ENABLE_FEISHU=n` in `sdkconfig.defaults`).
  When enabled, operates webhook receiver on :18790 with NVS credentials.
- **Serial CLI** (`cli/serial_cli.c`) — local console for setup/debug, works
  without WiFi.
- **WebSocket gateway** (`gateway/ws_server.c`, :18789) — LAN clients for
  live inspection.

### 3.6 Connectivity & onboarding

`wifi/wifi_manager.c` (credential store, non-blocking `esp_timer` exponential backoff, universal WPA/WPA2/WPA3 compatibility with disabled PMF to prevent router re-auth expiration, clean deauth on reboot) and `onboard/wifi_onboard_ble.c`
(BLE Wi-Fi provisioning via Apache NimBLE and Protocomm with Security 0 plain text;
allows personal hotspot onboarding without app via Web Bluetooth, releases BT RAM
on completion; mandatory onboarding mechanism for the device — entered on boot
when credentials fail after retries or triggered on-demand via `forget_wifi` voice/chat tool
or serial CLI `wifi_prov`). Boot-time AP scanning was removed to guarantee clean, collision-free
direct station connection in ~1-3s. Legacy SoftAP captive portal has been completely retired.
`ota/` for firmware updates.
---

## 4. Backend (`espet-backend/`)

### 4.1 Roles

| Role | How |
|---|---|
| **MCP host/router** | Connects to Google's official remote MCP servers (Gmail, Calendar, Docs, Drive) listed in `config/mcp_servers.json`; auto-discovers all tool schemas at startup into one registry (`mcp/client.py`, `mcp/registry.py`) |
| **Single tool endpoint** | `POST /api/mcp/execute` forwards `{tool_name, arguments}` to whichever MCP server owns that tool — adding a new MCP server needs **zero code changes** on backend or firmware |
| **Auth manager** | One-time Google OAuth consent at `/auth/google` (launched from the dashboard's **Login with Google** popup); access + refresh tokens in `db/tokens.db`, refreshed automatically. After a re-auth the dashboard calls `POST /api/mcp/reconnect` to re-discover tools with the fresh token — no backend restart needed. The ESP32 only ever presents the shared device API key |
| **Config source of truth** | `config_store.py` + `data/config.json` — everything the web dashboard edits (see §5) |
| **Dashboard host** | Serves the control center UI at `/` (`api/ui_router.py` + `static/`) |

### 4.2 API surface

| Method | Endpoint | Auth | Purpose |
|---|---|---|---|
| `GET` | `/` | — | Web dashboard |
| `GET` | `/health` | — | Status + connected MCP servers/tool counts |
| `GET` | `/auth/google` · `/auth/status` | — | OAuth login + link state (status also reports `token_valid` + `token_updated_at`) |
| `GET` | `/api/tools` | device key | Full tool schema list (feeds the agent's system prompt) |
| `POST` | `/api/mcp/execute` | device key | Execute any remote tool |
| `POST` | `/api/mcp/reconnect` | device key | Re-run MCP discovery with the current token (after Google re-auth) |
| `GET` | `/api/config` | device key | **Config bundle the device pulls** |
| `GET` | `/api/config/version` | device key | Per-section versions (cheap poll) |
| `PUT` | `/api/ui/profile` · `/soul` · `/memory` | device key | Edit USER.md / SOUL.md / MEMORY.md |
| `PUT` | `/api/ui/device` | device key | Name, timezone (IANA + generated POSIX TZ), sync interval |
| `GET/POST/PUT/DELETE` | `/api/ui/jobs[/{id}]` | device key | Web-managed cron jobs |
| `GET/POST/PUT/DELETE` | `/api/ui/skills[/{id}]` | device key | Web-managed skill markdown files |
| `GET` | `/api/skills/sync` | device key | Skill bundle the device pulls when `versions.skills` advances (enabled skills only) |
| `GET/PUT` | `/api/ui/avatar` | device key | Agent avatar dress-up state (dashboard-only) |
| `POST` | `/api/voice/transcribe` | device key | Raw 16 kHz mono PCM (or WAV) in → faster-whisper STT → `{"text"}` out |
| `POST` | `/api/voice/tts` | device key | `{"text"}` in → edge-tts + ffmpeg → 16 kHz mono 16-bit WAV out |
| `GET` | `/api/voice/last-capture-audio` | — | Most recent 16 kHz mono 16-bit WAV captured from hardware mic |
| `WS` | `/api/cloud/ws/device` | query `?key=` | Bidirectional WebSocket link for hybrid device operation |
| `WS` | `/api/camera/ws/device` | query `?key=&device_id=` | ESP32 live camera binary JPEG ingress |
| `GET` | `/api/camera/stream` | query `?key=` | Browser MJPEG stream; viewer count starts/stops ESP32 camera streaming |
| `GET` | `/api/camera/snapshot` | query `?key=` | Latest JPEG frame from backend camera buffer |
| `GET/POST/PUT` | `/api/camera/status` · `/stream/start` · `/stream/stop` · `/config` | device key | Camera stream status, manual control, and raw/processed mode config |
| `GET/POST/DELETE` | `/api/camera/owner-face/status` · `/owner-face` · `/owner-face/preview` · `/owner-face/test-detect` | device key / query key for preview | Local owner face enrollment/status/deletion/preview and camera-sampled owner detection test using optional InsightFace + ONNXRuntime embeddings |
| `GET/POST` | `/api/cloud/bindings` · `/bind` · `/unbind` | device key | Manage Telegram `chat_id` to `device_id` routing bindings |
| `GET/PUT` | `/api/cloud/gateway-config` | device key | View/update runtime Telegram bot token and LLM relay settings |
| `POST` | `/api/cloud/test-telegram` · `/test-llm` | device key | Live diagnostic tests for bot token and LLM endpoint |
| `POST` | `/api/cloud/record/start` · `/stop` | — | Remotely trigger ESP32 manual mic recording and Whisper transcription |
| `GET/POST` | `/api/cloud/voice-listen` | — | Query/toggle ESP32 autonomous VAD wake word listener state |
| `POST` | `/api/cloud/tts-say` | device key | Push `tts_say` frame to connected ESP32 to test MAX98357A speaker + OLED |
| `GET` | `/api/voice/debug/config` · `/voices` | device key | Inspect active STT/TTS settings, `last_capture.wav` stats, and preset voices |
| `POST` | `/api/voice/debug/transcribe` · `/tts` · `/regression` | device key | Debug STT (last capture / WAV / bypass flags), TTS with WAV/latency headers, and non-destructive regression suite |
| `GET` | `/cloud` | — | Web configuration dashboard for hybrid cloud gateway |
| `GET` | `/stt` | — | Voice & speaker hardware debug/testing bench (INMP441, MAX98357A, browser mic/file STT, TTS, regression suite) |

Auth model: one shared **device API key** (`Bearer`, set in `.env` and
`main/mimi_config.h`) for everything — single-user scope by design. CORS open
on LAN; put TLS in front before leaving the local network.

### 4.3 What the device-pull bundle looks like

`GET /api/config` (≈1–5 KB, fits the 32 KB device buffer with headroom):

```json
{
  "versions": { "user": 2, "soul": 1, "memory": 1, "device": 2, "jobs": 3, "skills": 4 },
  "user_md":   "# User Profile\n\n- Name: Jeff\n...",
  "soul_md":   "I am MimiClaw, a personal AI assistant...",
  "memory_md": "# Long-term Memory\n...",
  "device": {
    "name": "MimiClaw",
    "timezone": "Asia/Kuala_Lumpur",
    "posix_tz": "KUALAL-8",
    "sync_interval_s": 120
  },
  "jobs": [ { "...": "exact cron.json job schema, see §3.4" } ]
}
```

The bundle carries only the `skills` **version** — skill *content* is pulled
separately from `GET /api/skills/sync` (`{version, skills:[{id, name,
filename, content}]}`, enabled only) so the 32 KB config buffer is never at
risk. Limits mirrored on both ends: max 16 skills, 8192 bytes content each,
filename slug `skills/<name>.md`.

---

## 5. Config Sync — how web edits reach the device

The device is a pure HTTP **client** (no inbound connections), so sync is
pull-based with per-section versioning:

```
Web dashboard                 Backend                     ESP32
─────────────                 ───────                     ─────
edit profile  ──PUT──▶  bump versions.user (n+1)
                              │
                              ◀────── GET /api/config ────── (boot + every interval)
version n+1 > applied n  ──▶  write USER.md to SPIFFS
                              save sync_state.json
                              (unchanged sections are NOT rewritten)
edit skills   ──PUT──▶  bump versions.skills (n+1)
                              │
                              ◀── GET /api/skills/sync ──── only when version advanced
                              write /spiffs/skills/*.md, unlink removed
```

Rules implemented in `main/sync/config_sync.c`:

1. **Per-section versions** (`user`, `soul`, `memory`, `device`, `jobs`,
   `skills`) — a section is applied only when its version is strictly greater
   than the last applied value, persisted in `/spiffs/config/sync_state.json`.
2. **Markdown sections** are overwritten on device (backend is source of truth).
3. **Timezone**: backend converts the IANA zone to a POSIX TZ string; device
   applies it with `setenv("TZ") + tzset()` so cron "at" jobs and timestamps
   are local. (DST rules are simplified to the standard-time offset.)
4. **Cron merge** (protects voice-created jobs):
   - the sync state remembers which job IDs came from the web (`synced_ids`);
   - on pull: web jobs are upserted by ID (schedule change ⇒ `next_run`
     recomputed), web-deleted jobs are removed;
   - local jobs **not** in `synced_ids` (agent-created via `cron_add`) are
     left untouched;
   - afterwards `cron_reload_jobs()` reloads the file into the running service.
5. **Skills ownership** (mirrors the cron contract, tracked by filename):
   - the sync state remembers which skill filenames came from the web
     (`synced_skills`); web skills are written/overwritten, web-deleted or
     web-disabled skills are unlinked from `/spiffs/skills/`;
   - skills flashed at build time (`spiffs_data/skills/`) or created on-device
     by the agent (`write_file`) never enter that list and are never touched;
   - skill content is pulled from `GET /api/skills/sync`, not the config
     bundle (size); the section is applied in `sync_skills()`, outside the
     generic `apply_section` loop.
6. **Memory semantics**: a dashboard edit wins *once* (version bump), after
   which the agent freely consolidates memory again — the UI shows this warning.
7. **Avatar is dashboard-only**: the `avatar` config section (3D agent look)
   is version-tracked in the store but **excluded from the device bundle** —
   the ESP32 never pulls it.

---

## 6. Web Dashboard (`espet-backend/`)

**React 19 + antd 6 + `@lobehub/ui` SPA** (2026-09 redesign). Source lives in
`espet-backend/src/` (TypeScript/TSX); esbuild bundles it to
`static/vendor/app/` (ESM + code-splitting — the entry is ~13 KB, lobe-editor's
Lexical/shiki/mermaid and lobe-charts/recharts are lazy chunks). The committed
bundle is served by FastAPI exactly as before (`GET /` → `static/index.html`,
`/static` mount); **no backend API changes were made** — the SPA consumes the
same `/api/config`, `/api/ui/*`, `/health`, `/auth/*`, `/api/tools` endpoints.
The 3D RobotExpressive viewer is unchanged (`static/vendor/toy.bundle.js` global,
wrapped by `<ToyAvatar>`). Build: `npm run build:app` (also `build:vendor` for
the 3D toy).

**Brand + visual system (2026-09 design pass):**
- **Monochrome system**: black + white only. Canvas `#050507` with a moving
  dimmed-white-on-black gradient (4 white orbs on full-viewport paths); every
  container is a dimmed-white glass overlay (`rgba(255,255,255,0.05-0.08)` +
  backdrop blur). The one chromatic exception: **primary buttons** are a very
  dim light blue (`rgba(96,165,250,0.22→0.14)` gradient, `#dbeafe` text).
  Status is the one hue exception besides primary buttons: **dimmed semantic
  colors** — success/live/online = mint `#8fd9ae`, failure/offline = red
  `#eda7a7`, warning/attention = yellow `#e6cf94`, general = glass black+white
  (antd `colorSuccess/colorError/colorWarning` set to these in `theme.tsx`, so
  Badge dots, Alerts, danger buttons and toasts inherit them).
- **Logo**: `static/brand/espet_logo.jpg` — 46 px in the nav, 40 px in the Chat
  header, 128 px in the pre-mount boot splash, and the favicon.
- **Fonts**: default UI font (Outfit) for all rendered content — including
  markdown *previews*; **JetBrains Mono only for raw markdown sources** (the
  Edit textareas) and technical values (`.espet-mono`, antd `fontFamilyCode`).
- **Main buttons + selection controls**: the dim light-blue accent family —
  global CSS on `.ant-btn-primary` (dim blue gradient) and `.ant-switch-checked`
  (blue gradient track); `Segmented` + solid `Radio.Button` selected states and
  `::selection` use `rgba(147,197,253,0.22)` bg + `#dbeafe` text via antd
  component tokens. Spring lift on hover, desaturated disabled. Global
  `controlHeight: 40` so inputs/selects/buttons share one size (addonBefore
  groups align with their Save button); antd `Button` tokens widen padding
  (`paddingInline:18`).
- **Status**: semantic glass pills — `StatusChip` (`StatusChips.tsx`, 13 px,
  7/15 padding, colored dot + optional pulse, **Title Case** text: "Not
  Connected", "Backend Online", "Unsaved Changes"). Meta badges use `ui.tsx`
  `Badge` tone `default/success/warning/danger`.
- **Markdown inputs — one unified editor**: `SourceField`
  (`components/SourceField.tsx`) everywhere, **no View/Edit Segmented anymore**.
  Small "Content (markdown)" caption above antd Tabs **Edit | Preview** (Edit
  default): Edit = raw markdown source in an exact-byte JetBrains Mono textarea
  (so `#`, `**`, `-` survive verbatim to the device; the Lexical WYSIWYG was
  dropped for this reason and `@lobehub/editor` is no longer imported),
  Preview = read-only `@lobehub/ui` `Markdown` in the default UI font. Skills
  passes a `byteLimit` (8 KB counter); `canonical()` (whitespace-tolerant dirty
  compare) lives in SourceField. Profile keeps its outer Fields ↔ Markdown tabs
  that convert the structured form ↔ raw USER.md.
- **Wording**: the product is **ESPET** everywhere in the UI (views, routes,
  default SOUL/MEMORY/skill text, device-name default in `config_store.py` and
  the seeded `data/config.json`); **MimiClaw appears only as the credit** —
  footer "ESPET Control Center · powered by MimiClaw" (firmware C identifiers
  `mimi_*` and docs keep the name as internal/credit usage).

**Theme note:** uses `antd-style`'s `ThemeProvider` directly, not
`@lobehub/ui`'s wrapper (lobe-ui 5.40.3's ThemeProvider throws in a useMemo
outside its expected context). `createStyles`/`useTheme` come from `antd-style`.

**Navigation:** floating **glass pill** on desktop (`motion` spring entrance,
gentle scale-shrink on scroll, magic-move gradient pill under the active item via
`layoutId`) — brand logo · Home · Agent · Chat · Assistant · System (no status
chips in the nav). **Assistant and System are
single scroll pages, not dropdowns** (2026-09): clicking them navigates to a
page stacking the old views in order — Personality → Profile → Memory →
Schedule → Skills, and Device → Tools. A fixed **right-side bubble rail**
(`SectionRail`) joins solid 36 px icon bubbles (32 px mobile) with a vertical
white line that passes through their centres; scroll-spy (probe at 35%
viewport, ResizeObserver re-measure while lazy content loads) lights the
bubble of the section you're in; tapping one smooth-scrolls and deep-links
`#assistant/<section>` via replaceState. The label pill is collapsed to zero
width except on the **active** bubble and slides open on **hover** — the rail
never crowds the content. Bare section hashes (`#memory`, `#device`) alias to
their page+section. Under 820px
the top bar collapses to a floating brand pill + **bottom glass dock** that
navigates directly (no dropdowns/sheets anymore). A `--espet-nav-h` spacer keeps
content clear of the fixed bar (Chat height uses it too). Scroll rules:
`html { overflow-x: clip; overflow-y: auto }`, `body { overflow-x: clip }`,
`#root { min-height: 100% }` — the viewport scroll lives on `html` so antd
Modal's body scroll-lock can't reset it (fixed: opening New job / New skill
modals no longer jumps the page to top); `hidden` (instead of `clip`) would
make body a scroll container and break `window.scrollY`.

| View | What it does |
|---|---|
| **Home** (Overview) | New-owner landing: onboarding checklist (backend / Google / personalised / device address / owner face), Owner Profile card with local owner-photo upload + preview + remove controls + background watch cadence + Test Detection button that samples the camera and only sends welcome TTS/OLED after a fresh owner match, stat cards, **charts** (tools-per-server donut, client-sampled health sparkline — the server sync-activity chart awaits the metrics endpoint), device card, Google OAuth popup + MCP reconnect |
| **Cloud** | Dedicated configuration view for Telegram gateway poller, LLM proxy relay, and chat bindings |
| **Chat** | **Direct browser → ESP32 WebSocket gateway** (`ws://<device-ip>:18789`, `ws_server.c` protocol) — transparent prompt/response log, typing indicator (60s timeout), offline state; transcript in localStorage. Device IP set in Device view. (Backend relay + heartbeat = next plan) |
| **Camera** | Live view of the OV2640 through the backend relay: `<img>` consumes `/api/camera/stream` MJPEG, the backend reference-counts viewers and sends `camera_stream_start/stop` over `/api/cloud/ws/device`, and the ESP32 sends raw JPEG frames to `/api/camera/ws/device`; Snapshot opens `/api/camera/snapshot`, FPS selector supports 2/5/8/12 FPS, AI Overlay toggles raw vs processed mode, stale-frame detection auto-restarts the stream when `last_frame_age_ms` exceeds the live threshold, and optional local owner-face matching shows Owner Detected + Last Seen when InsightFace/ONNXRuntime are installed (enrollment lives on Home). Owner matching also runs in backend background watch mode: low-FPS short sampling windows trigger without a dashboard viewer. Owner welcome TTS fires only after consecutive confirmed matches, a configured absence period, and cooldown |
| **Profile** | USER.md — **Fields / Markdown** tabs: structured form (name/language/timezone/notes) or raw USER.md source editing (both convert on tab switch) |
| **Assistant** (scroll page) | One page stacking Personality → Profile → Memory → Schedule → Skills with the bubble rail; each section keeps its own dirty tracking + save flow |
| **Personality** (section) | SOUL.md — unified `SourceField` **Edit / Preview** tabs (Edit = raw mono source, Preview = lobe Markdown render) + reset-to-default + dirty/revert |
| **Memory** (section) | MEMORY.md — same unified Edit / Preview editor + quick-note append + clear-with-confirm |
| **Skills** | Skill CRUD; editor uses an **exact-byte source textarea + lobe Markdown preview** (Edit/Preview tabs, not WYSIWYG) so the `# Title` contract and 8192-byte cap survive to the device |
| **Schedule** | Cron job CRUD (antd Form modal: recurring/one-time, device/Telegram delivery) |
| **Device** | Name, timezone, sync interval, **live-chat device address** (localStorage), channel overview, config-version pull table |
| **Tools** | Searchable/filterable MCP tool explorer + JSON schema modal; `@lobehub/icons` brand marks |

UX: dirty-state tracking with unsaved-changes navigation guard, toasts
(antd `App` context), confirm dialogs, `prefers-reduced-motion` respected,
cache-busted entry (`?v=<hash>` stamped by the build). **Visual tokens are
standardized in `DESIGN.MD` (repo root) — the single source of truth; any
frontend UI change must follow and update it (AGENTS.md "Design Token
Rule").** Persistent status chips (config version, backend health) live in a
fixed bottom-right **StatusDock** on desktop (mobile keeps them in the top
brand pill); **"Unsaved Changes" shows on the container being edited**
(`SectionDirtyChip` in the card header of Personality/Profile/Memory/Device),
never in the nav. The
footer is a hairline-bordered monochrome line: "ESPET Control Center · powered
by MimiClaw".


---

## 7. End-to-end flows

### 7.1 Voice pipeline (hands-free: "Hey Mimi" / "Mimi" → software wake verification → command → spoken reply)

Wake word runs via **VAD + Backend Whisper wake word verification**:
The ESP32 runs continuous low-overhead energy VAD listening (32 ms frames, adaptive threshold:
`max(420, 1.33× ambient floor)` capped at 14000, 3 consecutive frames ≈ 96 ms required).
The ambient floor is a min-statistics estimator (instant fall, bounded rise ~750 RMS/s) that
runs on **every** frame, so it converges even in a loud room where all frames exceed the fixed
threshold. When speech is detected, it captures the utterance with a 250ms pre-roll (rewinding
the I2S ring buffer by 4000 samples so the wake word prefix is preserved intact) until 700ms
silence or 2.5 s cap, then sends it to the FastAPI backend (`POST /api/voice/transcribe`).
Whisper (with a **neutral/empty** `initial_prompt` — wake words in the prompt were verified to
bias the decoder into hallucinating the wake word from noise/echo) verifies whether the utterance
begins with any configured wake word (e.g., `"mimi"`, `"hey mimi"`, `"expert"`, `"hey expert"`,
`"espet"`, `"米米"`), then applies a **confidence gate** (`voice_min_avg_logprob` −1.1,
`voice_max_no_speech_prob` 0.75) plus a pre-Whisper RMS gate (`voice_min_rms` 250) to reject
hallucinated wake words:
- If wake word does NOT match (or is rejected by the confidence gate): silently ignored.
- If one-shot command ("Hey Mimi turn on the LED"): backend strips the wake word (repeated
  leading wake words are also stripped, e.g. "米米 米米" → no command) and returns the command
  text; ESP32 queues it immediately to the ReAct agent loop!
- If wake word only ("Hey Mimi"): ESP32 plays the pre-cached "Yes?" ack, waits `ACK_SETTLE_MS`
  (450 ms) for the ack echo to decay, then opens the command window.
Agent replies are routed via `voice_client_speak()` → `POST /api/voice/tts` → MAX98357A speaker.

**Generation-Scoped Wake-Word Barge-In, Speaker Serialization & Echo Guard**:
The voice client separates local VAD capture state (`s_vad_busy`) from active turn tracking (`s_agent_turn_active` + monotonic `uint32_t turn_gen` propagated via `mimi_msg_t`):
- The VAD wake listener pauses while `s_vad_busy` is `true`, while speaker audio/TTS is active (`tool_audio_is_speaker_busy()`, derived under `s_audio_mux` from refcounted `s_speaker_session_depth > 0 || s_playing || s_cache_clearing`), when manual/active recording runs (`tool_audio_is_busy()`), or during the post-playback echo guard (`mic_guard_active()`, 700 ms).
- Because `s_agent_turn_active` does not pause VAD listening, saying a wake word while `agent_loop_task` is thinking triggers `voice_abort_if_turn_active()` to advance `s_aborted_gen` monotonically (`turn_gen <= s_aborted_gen`). Stale turns in `agent_loop.c`, `outbound_dispatch_task` (`mimi.c`), and `tool_audio_play_wav_turn()` drop cleanly without aborting `'Yes?'` (`turn_gen = 0`), standalone cues (`tts_say`, `snap_process_task`, `audio_spktest`), or the newly queued command.
- Concurrent speaker callers (`outbound_dispatch_task`, `snap_process_task`, `tts_say_task_fn`, `ack_play`) serialize strictly on `s_tx_mutex` during I2S TX (never across HTTP downloads), and `play_tone_wait()` / `tool_audio_play_wav_turn()` drain ~160 ms of silence through the TX DMA ring before unparking `mic_feeder_task`.
- After playback, `voice_client_speak_with_display_turn()` arms a **700 ms echo guard** (`ECHO_GUARD_MS`): VAD is skipped and the ring/DMA are flushed until the amp tail + room reverb decay.
- Guard timers: A 60 s turn watchdog clears orphaned `s_agent_turn_active` flags, and a 32 s audio recording safety watchdog disarms hung recording states.

```
[LISTEN ] voice_wake task monitors mic stream via energy VAD (32 ms frames, adaptive threshold,
          floor-tracked: max(420, 1.33×ambient), cap 14000; 3 consecutive frames to trigger)
[CAPTURE] Speech detected → arm capture with 250 ms pre-roll from ring buffer
          Captures until 1200 ms silence (adaptive cutoff ≈1.25×ambient) or 15 s utterance cap
[VERIFY ] POST /api/voice/transcribe ──▶ backend faster-whisper (neutral prompt)
          ├─ rms < voice_min_rms ──▶ skip STT, no wake
          ├─ Wake not matched ──▶ silent ignore, flush ring, resume listening
          ├─ Wake matched (confidence ok) ──▶ if s_agent_turn_active, abort old gen; assign new_gen
          ├─ One-shot command ("Hey Expert what's the weather") ──▶ push to agent bus (turn_gen = new_gen)
          └─ Wake only ("Hey Expert") ──▶ play "Yes?" ack (turn_gen=0) ──▶ ACK_SETTLE_MS (450 ms) ──▶ record follow-up
              (if no command/only wake word heard, ignore & clear cache, do not queue to agent)
[ THINK ] Agent ReAct loop executes tools & LLM API (checks voice_client_is_turn_aborted(msg.turn_gen))
[ SPEAK ] voice_client_speak_turn(): POST /api/voice/tts {"text"} (refcounted speaker session)
          ──▶ backend edge-tts + ffmpeg ──▶ 16 kHz WAV ──▶ s_tx_mutex ──▶ MAX98357A 🔊 + 160 ms DMA drain
          ──▶ playback done ──▶ ECHO_GUARD_MS (700 ms) ──▶ tool_audio_clear_cache() ──▶ turn_finished(gen) ──▶ resume LISTEN!
```

Echo control: the listener pauses while the speaker is active (`tool_audio_is_speaker_busy()`),
during manual/active recording (`tool_audio_is_busy()`), or while the post-playback echo guard is active,
so the device never wakes on its own voice.
Configurable in `espet-backend/config.py` / `.env`: `voice_wake_words` (default: `"expert,hey expert,hi expert,espet,hey espet,mimi,hey mimi,hi mimi,ok mimi,hi esp,米米,咪咪,小米"`),
`voice_stt_model` (default `base`), `voice_stt_device` (default `auto` — CUDA with CPU fallback),
`voice_stt_beam_size` (default `1` — greedy fast decoding), `voice_stt_compute_type` (default `auto`),
`voice_language` (None = auto), `voice_tts_voice`, `voice_stt_prompt` (default empty — never put wake words here),
`voice_min_avg_logprob`, `voice_max_no_speech_prob`, `voice_min_rms`.
### 7.2 Reactive (Jeff): *"Email Dr Wong a summary of my proposal doc"*

```
voice → INMP441 → agent_loop
  ctx = SOUL + USER + MEMORY + transcript + tools
  LLM #1 → tool_use(docs_read_document)  → POST /api/mcp/execute → Google Docs MCP
  LLM #2 → tool_use(gmail_send_email)    → POST /api/mcp/execute → Gmail MCP
  LLM #3 → end_turn("Done — summary sent.")
speaker reply
```

### 7.3 Proactive (Herman): scheduled briefing

```
cron.json job due → cron_service pushes "Give me a summary of my day"
→ agent loop (identical to a user message)
→ tools (calendar_list_events, gmail_search_threads…) → reply to channel
  (system = voice, or telegram chat_id)
```

### 7.4 Configuration: dashboard → device

See §5. Typical latency = next sync tick (device also pulls on every boot).

---

## 8. Repository map

```
espet/
├── main/                      firmware (ESP-IDF component)
│   ├── mimi.c                 boot + service wiring
│   ├── mimi_config.h          all compile-time constants & pin map
│   ├── mimi_secrets.h         wifi/keys (gitignored; see .example)
│   ├── agent/                 agent_loop, context_builder
│   ├── llm/                   llm_proxy (provider abstraction, streaming)
│   ├── tools/                 local tools + tool_registry (local/remote dispatch)
│   ├── cron/  heartbeat/  sync/    proactive daemons + config pull
│   ├── memory/                memory_store, session_mgr
│   ├── channels/              telegram_bot, feishu_bot
│   ├── gateway/  cli/  onboard/  ota/  wifi/  proxy/  bus/  skills/
│   ├── voice/                 voice_client (WakeNet "Hi ESP" → STT → bus, TTS → speaker)
├── spiffs_data/               default SOUL.md · USER.md · MEMORY.md · skills
├── espet-backend/
│   ├── main.py                FastAPI app wiring
│   ├── config_store.py        data/config.json store + versioning + TZ/jobs helpers
│   ├── api/                   health · mcp_router · tools_router · auth_router
│   │                          config_router (device pull + UI CRUD) · ui_router (dashboard)
│   │                          voice_router (STT faster-whisper · TTS edge-tts)
│   ├── auth/                  device_auth (Bearer key) · oauth_manager · token_store
│   ├── mcp/                   client + auto-discovery registry
│   ├── db/tokens.db           OAuth token store (SQLite)
│   ├── package.json           front-end vendor deps (animejs, three) + esbuild
│   ├── tools/toy-entry.js     esbuild entry → static/vendor/toy.bundle.js (3D agent)
│   ├── static/                index.html · style.css · app.js
│   │                          toy.bundle.js (three.js) · models/robot.glb
│   └── data/config.json       live config (gitignored, seeded on first run)
├── DESIGN.MD                  dashboard design system doc (tokens, components, layout)
├── docs/ARCHITECTURE.md       firmware-deep architecture
├── .agent/HERMAN_ROADMAP.MD   proactive-half gap analysis + prioritized roadmap
└── .agent/ESPET_PINS.MD       wiring table
```

---

## 9. Two-FYP split (how this repo maps to the reports)

Per `AGENTS V3.MD` (SPLIT v2): all hardware/driver bring-up is **shared L0
work**. Above that:

| | **Jeff — Reactive** | **Herman — Proactive** |
|---|---|---|
| Question | How can an edge agent parse multimodal requests and execute multi-step tools reliably? | How can an embodied agent initiate helpful actions and adapt without commands? |
| Owns (code) | `agent_loop.c`, `tool_registry.c`, `tool_*.c`, backend MCP router + OAuth | `cron_service.c`, `heartbeat.c`, `memory_store.c`, `context_builder.c`, delivery stack |
| Metrics | tool-selection accuracy, multi-step success, e2e latency | trigger precision/recall, false-interruption rate, memory retention |

The web dashboard + config sync span both halves (it configures Jeff's
tools/auth *and* Herman's schedules/memory) — treat it as shared platform
infrastructure in the reports, like L0.

Herman's full gap analysis and prioritized roadmap of needed tools/functions:
[`.agent/HERMAN_ROADMAP.MD`](./HERMAN_ROADMAP.MD).

---

## 10. Known limitations / next steps

- HTTP + shared API key on LAN — add TLS/reverse proxy before remote exposure.
- POSIX TZ carries no DST rules (fine for MY; 1h drift in DST zones).
- `/auth/status` reports "Linked" if token *records* exist even when the stored
  refresh token is dead (`invalid_grant`). Fix is one click: **Re-link Google**
  on the Overview → new token stored → `POST /api/mcp/reconnect` reconnects.
- Memory sync is "web wins once", not a merge — a proper CRDT-style merge is
  future work.
- Cron "at" jobs entered on the web use the dashboard's configured timezone;
  keep Device timezone and Profile timezone identical.
- The device applies config only while powered + connected; offline queues
  are not persisted.
- Voice STT/TTS runs on the backend only — voice flow silently degrades to
  error beeps when the server is unreachable; first utterance after backend
  boot is slow (Whisper model cold load).
- Voice command capture is single-buffered: re-triggering while the previous
  command is still uploading can overwrite the PCM being sent (future:
  double-buffer).
- Wake phrase is fixed to the free WakeNet models ("Hi ESP"); custom wake
  words require Espressif commercial models.

---

## Recent Changes

Append-only session log. Newest first. One line per change:
`- YYYY-MM-DD — summary (files: ...)`.

- 2026-11-20 — Fixed Wi-Fi reboot AUTH_EXPIRE (2) / AUTH_FAIL (202) and premature BLE onboarding fallback: (1) Added clean Wi-Fi teardown and deauth in `wifi_onboard_ble.c`: explicitly calls `esp_wifi_disconnect()` and `esp_wifi_stop()` with settling delays before `esp_restart()` so AP router receives 802.11 Deauth frame and clears client association table before reboot. (2) Configured universal Wi-Fi STA compatibility in `wifi_manager.c`: disabled PMF (`pmf_cfg.capable = false`) to eliminate 802.11w SA query re-auth failures on home routers; relaxed authmode threshold to `WIFI_AUTH_WPA_PSK`; enabled `WIFI_ALL_CHANNEL_SCAN` and `WIFI_CONNECT_AP_BY_SIGNAL`; added masked credential debug logging; added explicit disconnect before reconnect in `on_reconnect_timer()`. (3) Backoff & Boot Timeout Alignment: capped `MIMI_WIFI_RETRY_MAX_MS` to 8000ms in `mimi_config.h` (23s total backoff) and increased `wifi_manager_wait_connected` timeout in `mimi.c` from 30s to 60s, ensuring all 5 retries execute without getting prematurely aborted by BLE onboarding fallback (files: main/onboard/wifi_onboard_ble.c, main/wifi/wifi_manager.c, main/mimi_config.h, main/mimi.c, .agent/SYSTEM_OVERVIEW.md).

- 2026-11-14 — Removed "Speaking..." placeholder on OLED: updated `voice_client.c` and `mimi.c` to remove the static "Speaking..." display text, now directly rendering the spoken response text on the SSD1306 OLED across lines 1 and 2 during audio playback (files: main/voice/voice_client.c, main/mimi.c, .agent/SYSTEM_OVERVIEW.md).

- 2026-11-12 — Disabled direct owner welcome bypass endpoint: `/api/camera/owner-face/test-welcome` now returns a disabled error and no longer sends TTS/OLED, preventing stale dashboard bundles from triggering Owner Detected without camera verification; frontend keeps only Test Detection (`/test-detect`) (files: espet-backend/api/camera_router.py, espet-backend/src/api.ts, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Changed Owner Profile test from bypass welcome to true detection test: new `/api/camera/owner-face/test-detect` temporarily starts camera sampling, waits for a fresh owner face match, and only then sends the welcome TTS/OLED action; Home button now says Test Detection and reports detected/action/offline outcomes (files: espet-backend/api/camera_router.py, espet-backend/src/api.ts, espet-backend/src/views/Home.tsx, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Added Owner Profile "Test Welcome" web button: new `/api/camera/owner-face/test-welcome` endpoint sends the same `tts_say` owner welcome frame to the ESP32 without waiting for a face match, letting the dashboard verify speaker + OLED `Owner Detected` / `Welcome Back` behavior manually (files: espet-backend/api/camera_router.py, espet-backend/src/api.ts, espet-backend/src/views/Home.tsx, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Fixed owner welcome trigger after consecutive face matches: preserved the initial absent/return state across the confirmation window so the second confirmed match can trigger TTS, and accepted `camera_stream_status` frames in the cloud router to avoid harmless unknown-frame errors during background camera watch (files: espet-backend/services/camera_processor.py, espet-backend/api/cloud_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Added explicit OLED lines for owner welcome TTS: firmware `tts_say` now accepts `display_title` / `display_body` and calls `voice_client_speak_with_display()` so the speaker can say the configured welcome text while OLED shows `Owner Detected` / `Welcome Back` instead of a hard-split sentence (files: main/voice/voice_client.h, main/voice/voice_client.c, main/channels/cloud_link/cloud_link.c, espet-backend/api/camera_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Added backend background owner watch so owner detection is no longer tied to an open Camera page: FastAPI lifespan starts/stops an owner-watch task that periodically opens a low-FPS camera stream for enrolled online devices, samples for a few seconds, and stops it when no dashboard viewer is active; Home Owner Profile shows the background watch cadence (files: espet-backend/api/camera_router.py, espet-backend/main.py, espet-backend/config.py, espet-backend/services/owner_face_service.py, espet-backend/src/api.ts, espet-backend/src/views/Home.tsx, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Added owner welcome TTS trigger: backend owner detection now maintains confirmed-match count, absence threshold, and cooldown (`camera_owner_welcome_*` settings) and sends `tts_say` over the device cloud WebSocket when the owner returns; firmware cloud link handles `tts_say` in a detached task using `voice_client_speak()` so the ESP32 speaker says the welcome line without blocking the WS event loop (files: espet-backend/services/camera_processor.py, espet-backend/api/camera_router.py, espet-backend/config.py, espet-backend/src/api.ts, main/channels/cloud_link/cloud_link.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Moved Owner Profile setup from Camera to Home and added owner photo preview: backend now stores the uploaded owner image beside the embedding and serves `/api/camera/owner-face/preview`; Home shows Owner Profile with preview, upload/remove controls, enrollment checklist item, and dependency status; Camera now only displays Owner Detected / Last Seen and links back to Owner Settings (files: espet-backend/services/owner_face_service.py, espet-backend/api/camera_router.py, espet-backend/src/api.ts, espet-backend/src/views/Home.tsx, espet-backend/src/views/Camera.tsx, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Added local owner face enrollment/detection for Camera: optional InsightFace + ONNXRuntime service stores per-device owner embeddings under `data/owner_faces/`, adds `/api/camera/owner-face` upload/status/delete endpoints, runs owner matching about once per second on live frames, and Camera page shows Owner Detected, Owner Enrolled/Not Set, similarity, and Last Seen time (files: espet-backend/services/owner_face_service.py, espet-backend/services/camera_processor.py, espet-backend/api/camera_router.py, espet-backend/config.py, espet-backend/requirements.txt, espet-backend/src/api.ts, espet-backend/src/views/Camera.tsx, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Hardened live camera stream against stuck frames: firmware stream task now drops failed sends and reconnects the camera WebSocket while streaming remains enabled; backend camera status reports `last_frame_age_ms` and the MJPEG generator nudges `camera_stream_start` if no new frame arrives for 3 s; dashboard marks streams as Stalled and auto-reloads the MJPEG URL while re-requesting stream start (files: main/tools/tool_camera.c, espet-backend/services/camera_processor.py, espet-backend/api/camera_router.py, espet-backend/src/api.ts, espet-backend/src/views/Camera.tsx, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Fixed firmware link failure when `CONFIG_BT_ENABLED` is disabled: `wifi_onboard_ble.c` now compiles BLE provisioning only when Bluetooth is enabled and otherwise falls back to SoftAP captive-portal onboarding, avoiding undefined `wifi_prov_scheme_ble` symbols (files: main/onboard/wifi_onboard_ble.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-12 — Implemented backend-relayed real-time camera streaming: firmware camera mutex now arbitrates still photos, video recording, and live stream capture; cloud link accepts `camera_stream_start/stop`; new FastAPI camera processor/router ingests binary JPEG frames on `/api/camera/ws/device`, serves MJPEG `/api/camera/stream`, snapshots, status, and manual stream/config endpoints; Camera dashboard now uses backend MJPEG with FPS and raw/CV controls; added synthetic backend smoke test script (files: main/mimi_config.h, main/tools/tool_camera.c, main/tools/tool_camera.h, main/channels/cloud_link/cloud_link.c, espet-backend/services/camera_processor.py, espet-backend/api/camera_router.py, espet-backend/config.py, espet-backend/main.py, espet-backend/src/api.ts, espet-backend/src/views/Camera.tsx, espet-backend/static/index.html, espet-backend/scripts/test_camera_stream.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-09 — OLED cold-boot readiness fix: added 100 ms power stabilization delay and 5-attempt retry loop (50 ms spacing) in display_status.c to accommodate SSD1306 power-on reset / charge pump ramp-up latency on USB cold plug (files: main/display/display_status.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — STT recognition accuracy & tolerance upgrade: upgraded backend Whisper default from 'base' (74M) to 'small' (244M); enabled beam search (beam_size=3, best_of=3) and context priming initial_prompt in voice_router.py to drastically improve accent and conversational tolerance; added +8 dB (2.5x) digital gain to capture_task in tool_audio.c so audio_record_test captures healthy RMS amplitudes instead of low-signal whisper noise (files: espet-backend/config.py, espet-backend/api/voice_router.py, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Hybrid mode implementation (Plan C-Hybrid): offload Telegram long polling and credentials to FastAPI backend; persistent `/api/cloud/ws/device` WebSocket link; keyless LLM inference tunneling (`llm.request` / `llm.response`) via `cloud_link.c` and `llm_proxy.c`; runtime gateway settings persistence in `data/gateway_config.json` with dynamic poller restart; `/cloud` standalone configuration dashboard with live diagnostics; 10/10 automated host E2E tests verified (files: main/channels/cloud_link/cloud_link.c, main/channels/cloud_link/cloud_link.h, main/llm/llm_proxy.c, espet-backend/api/cloud_router.py, espet-backend/services/gateway_settings.py, espet-backend/static/cloud.html, espet-backend/tests/test_cloud_gateway.py, docs/HYBRID_MODE_PLAN.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-06 — Speaker-blur fix: `play_tone` (beeps) and
  `tool_audio_play_wav` (ack WAV) did NOT raise `s_speaker_busy`, so the
  mic feeder kept racing I2S TX during every beep/ack — the project's own
  field rule (concurrent mic reads corrupt TX) turned beeps and acks into
  noise ("blur sound"). Both players now gate the mic for the whole
  playback (idempotent with voice_client's outer session gate), so every
  speaker output parks the feeder by construction. Pin audit confirmed
  shared BCLK 38 / WS 39 full-duplex wiring is correct and conflict-free
  (files: main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-06 — Phantom camera-snap fix: saying "Hi ESP" auto-captured a photo
  to Telegram. Root cause: the wake-ack speaker burst (MAX98357A current
  spike) couples into GPIO 41 (camera button, weak internal pull-up) as
  edge pairs, and the white-button path had NO debounce — `DEBOUNCE_MS` and
  `s_last_snap_time` were declared but never used, so any LOW→HIGH pair
  < 600 ms apart counted as a short click. Added MIN_PRESS_MS 50 minimum
  pulse width (shorter pulses logged as "treated as noise") and a
  CLICK_LOCKOUT_MS 400 re-trigger lockout (files:
  main/tools/button_handler.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-06 — TTS-outage triage + self-heal: backend verified healthy
  end-to-end (TTS endpoint returns a 16 kHz WAV, mDNS advertises the current
  IP 192.168.0.179:8000, baked fallback IP matches, device keys match).
  Gap closed: mDNS discovery ran only once at boot (6 attempts) — a backend
  that was down at boot or moved IP afterwards left the device on a dead
  URL until reboot. Added `backend_discovery_invalidate()`; voice STT/TTS
  failure paths now drop the cached URL and re-discover in the background
  (files: main/sync/backend_discovery.c, main/sync/backend_discovery.h,
  main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-06 — Voice reliability fixes: (1) `ack_fetch()` leaked
  `s_speaker_busy` when `esp_http_client_init` failed → mic feeder parked
  forever, wake word deaf until reboot; session now released on all paths.
  (2) `voice_backend_url()` shared static buffer raced between the wake task
  (STT) and outbound task (speak) — now caller-owned buffers. (3) Command
  window: 5 s lead-in before the 1.6 s silence stop applies (was: 1.6 s from
  ack, cutting users off), speech RMS threshold 350→200 (soft trailing words
  no longer missed), no-speech capture skips the STT round-trip. (4) WakeNet
  partial-frame carry-over: a partially filled 512-sample model frame is no
  longer discarded on the next loop pass. (5) STT requests now send
  `X-Audio-Rate`; backend resamples if it differs from 16 kHz (was: silent
  assumption). (6) Feeder ring-full comment corrected (drops newest, not
  oldest) (files: main/voice/voice_client.c, main/tools/tool_audio.c,
  espet-backend/api/voice_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-06 — TTS language fix: default edge-tts voice switched from Cantonese
  zh-HK-HiuMaanNeural to Mandarin zh-CN-XiaoxiaoNeural (files:
  espet-backend/config.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-05 — OLED diagnostic: I2C + panel ACK verified via boot capture
  (display init OK, no assert) but panel stays black. Added boot self-test in
  display_hw_init: full-screen fill for 0.8 s — visible flash proves panel
  electrically OK (then software rendering issue); black means panel
  power/charge-pump fault (files: main/display/display_status.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-10-06 — OLED hunt hardened after field false-positive (flaky ACK on
  strap GPIO 0 / addr 0x3D locked the hunt onto bogus pins): require two
  consecutive ACKs, un-lock and re-hunt if init/refresh NACKs, and only
  delete the I2C driver when actually installed. Panel confirmed dead on all
  169 pin combos — module replacement pending (files:
  main/display/display_status.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-05 — Re-added SSD1306 as the text status display: display_status.c
  now drives the panel directly (I2C0 GPIO 2/1, two 16px text lines, no eye
  animation task) and re-registered the oled_print tool ({"text": "..."}).
  Warning: ssd1306 component asserts on runtime I2C failure — keep panel
  connected (files: main/display/display_status.c, main/tools/tool_registry.c,
  main/idf_component.yml, main/CMakeLists.txt, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-05 — Added main/display/display_status.{c,h}: display-agnostic
  two-line status module (serial-console output as placeholder; swap
  display_status_output() for the real driver when new display hardware is
  chosen). Wired into actions: boot Starting/Ready-idle, msg received,
  Thinking/Replying/LLM error (agent_loop), Tool:/Done:/Tool failed
  (tool_registry), Listening/Speaking/Voice err + idle restore (voice_client),
  WiFi FAILED (wifi_manager), Backend not found (backend_discovery)
  (files: main/display/display_status.{c,h}, main/mimi.c, main/agent/agent_loop.c,
  main/tools/tool_registry.c, main/voice/voice_client.c, main/wifi/wifi_manager.c,
  main/sync/backend_discovery.c, main/CMakeLists.txt, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-05 — Removed all SSD1306 OLED display code (crashing on i2c alloc
  assert in ssd1306 component): deleted tool_oled.{c,h}, dropped oled_print tool
  registration, oled_display_status calls, espressif/ssd1306 dependency, OLED
  prompt lines, and boot animation start. Display to be replaced later;
  GPIO 1/2 (I2C0) freed (files: main/tools/tool_oled.{c,h}, main/mimi.c,
  main/tools/tool_registry.c, main/agent/agent_loop.c, main/agent/context_builder.c,
  main/CMakeLists.txt, main/idf_component.yml, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-05 — (rev 2) mDNS A-record lookup fixed to bare hostname; discovery
  deferred to post-boot to avoid internal-RAM starvation crash.
- 2026-10-05 — Backend IP is no longer hardcoded at build time: backend
  advertises `_espet._tcp.local.` via python-zeroconf (new dependency) and the
  firmware resolves it at runtime with espressif/mdns (new managed component);
  compile-time `BACKEND_IP` remains the fallback. Fixes TTS/STT/config-sync
  breakage after router/IP changes (files: main/sync/backend_discovery.{c,h},
  main/sync/config_sync.c, main/voice/voice_client.c, main/tools/tool_registry.c,
  main/mimi.c, main/CMakeLists.txt, main/idf_component.yml,
  espet-backend/main.py, espet-backend/requirements.txt, package.json,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Feeder 8 KB .bss stack overflowed (field: StoreProhibited in
  uxListRemove via vTaskDelay from mic_feeder_task; kernel list corruption also took Telegram
  down). i2s_channel_read's TLS/queue call chain is deeper than budgeted. Feeder stack now
  12 KB in PSRAM via heap_caps_malloc + xTaskCreateStatic (no internal-RAM cost, no runtime
  fragmentation) (files: main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — mic_feed task creation failed in the field (same fragmented-internal-heap
  failure as voice_wake: "mic feeder failed to start: ESP_FAIL"). Feeder now uses
  `xTaskCreateStatic` with an 8 KB static .bss stack — no runtime allocation at all
  (files: main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Two ring-rollout bugs fixed via debug instrumentation: (1) voice_task's
  read buffer was WAKE_CHUNK_SAMPLES (480) but WakeNet consumes 512 — ring_read overflowed
  up to 32 samples past the PSRAM task stack, corrupting the debug counters (insane values,
  5 s report firing every 20 ms); buffer now 512. (2) mic_feed spin at prio 7 starved IDLE1
  (WDT every 5 s) because continuous DMA means i2s_channel_read returns instantly — added
  vTaskDelay(1) per 32 ms consumed (3x realtime pace, 4 s ring absorbs jitter) (files:
  main/voice/voice_client.c, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Ring-buffer integration actually landed this time: the previous session's
  edit script died on an assert before writing voice_client.c, so the field build still read
  I2S directly (frames=2/5s persisted, plus a latent bug — 64000-sample ring is not a power
  of two, `& MASK` wrapped wrongly; ring resized to 65536). voice_wake now truly consumes
  `tool_audio_ring_read`, feeder starts with voice_client_start (files:
  main/tools/tool_audio.c, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Wake-word audio starvation fixed with RX ring buffer: debug stats proved the
  mic path fine (peak_rms 11360) but WakeNet starved (frames=5/5s, paused/empty=850) — I2S DMA
  holds only ~128 ms (4×512-frame descriptors) and the prio-3 wake task loses the CPU to
  prio-5 network tasks, so DMA overruns between reads. New architecture: high-priority (7,
  core 1) `mic_feed` task shovels DMA → 4 s PSRAM ring (`tool_audio_rx_stream_start/
  ring_read/ring_available` in tool_audio); voice_wake now reads the ring at its own pace.
  Feeder started/stopped with the voice client (files: main/tools/tool_audio.{c,h},
  main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Wake-listen debug instrumentation (field: "Hi ESP" not triggering): voice_task
  now prints a rate-limited 5 s report — frames fed to WakeNet, avg/peak RMS, loud-frame count,
  paused/empty iterations — plus live `voice-level audio rms=N` lines when speech-level audio
  appears, an explicit NO-AUDIO-FRAMES warning if the mic stream is dead, and the model's
  det_threshold in the boot line. Diagnosis keys: frames=0 → RX plumbing; low peak → mic
  level/position; frames OK + no DETECTED → threshold/pronunciation (files:
  main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-10 — `audio_spktest` 4-stage speaker diagnostic added (tool_registry registered):
  1) 440 Hz full-volume tone (amp/wiring/power), 2) locally generated 1 kHz WAV through
  play_wav (no network), 3) 1 kHz volume ramp 1.0/0.4/0.15 (TTS speech sits at ~-24 dBFS —
  quieter than all steps; a faint-ramp result points at speaker sensitivity, fix is a
  playback gain), 4) full server-TTS pipeline via voice_client_speak. Verdict routing in
  serial log + JSON. Context: TTS was audible once, then went silent across firmware
  iterations; log shows successful downloads/plays, so the bisection splits hw vs
  parser vs pipeline (files: main/tools/tool_audio.{c,h}, main/tools/tool_registry.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Speaker-session gate fixes TTS "noise burst": field test (tone beeped OK,
  "ESPeting..." came out as static) proved the amp/mic wiring fine and isolated the fault to
  the feeder racing the TTS path — parking only during playback left the HTTP download phase
  (and its TLS/crypto CPU load) corrupting TX. New `tool_audio_speaker_session_begin/end`
  gates: RX feeder parks for download + playback + ack-cache windows. Hardware bisection
  recipe for future: `tool_exec audio_play_tone` (local sine, no server) separates amp
  issues from pipeline issues (files: main/tools/tool_audio.{c,h}, main/voice/voice_client.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — `npm run dev:mac` now starts the backend with hot reload
  (`uvicorn main:app --reload --host 0.0.0.0 --port 8000` — same as dev:win; python main.py
  had no auto-reload so voice_router edits needed a manual restart). Note: backend must be
  running for TTS/STT; device-side playback issues after the ring rework are being bisected
  with `tool_exec audio_play_tone` (local sine, no server) to split hardware vs software
  (files: espet-backend/package.json, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Three root causes fixed from probe data (`zero_reads=0, ring_written=870k`,
  but WakeNet saw rms=0): (1) THE no-detection bug — the ring refactor read into `chunk`
  while detect() consumes `s_wn_buf`: the model was fed calloc'd zeros; ring now reads
  straight into s_wn_buf. (2) Feeder harvested only ~25% of DMA frames (256-frame clamp on
  descriptor-granular reads) — buffer now 4096 frames (16 KB static), 200 ms read timeout,
  full harvest. (3) rx_read rewrote to consume the ring in BOTH modes — the feeder is the
  sole DMA owner (dual consumers raced: each got half). Added tool_audio_ring_flush(),
  called after busy-pause and on capture_arm so stale audio never enters a capture; mic_feed
  parks during playback (s_playing) to stop TTS stutter. Note: voice output was functionally
  intact — perceived slowness was TLS timeouts (RSSI -76) + feeder/playback race, both now
  addressed (files: main/tools/tool_audio.{c,h}, main/voice/voice_client.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Zero-audio probe for wake path: field showed ring feeding exact-zero frames
  to WakeNet (rms=0) while voice output stuttered. Added DMA->ring health counters
  (`s_feeder_reads/s_feeder_zeros/s_ring_written`, non-static, extern in voice_client) and a
  `[debug] feeder: ... frame[0..3]=` raw-sample dump in the 5 s listen report — pinpoints
  whether zeros originate at I2S DMA, the ring, or the slot extraction (files:
  main/tools/tool_audio.c, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Long-reply voice truncation fixed: 2nd voice reply (194-char Chinese text →
  262 KB WAV) exceeded the 256 KB TTS download buffer (field: `TTS FAILED ... oom=1` → error
  beep, no speech). TTS RX buffer raised to 2 MB (PSRAM), and `sanitize_tts_text()` now caps
  spoken text at 800 chars (+"…") ≈ 1.1 MB WAV / ~35 s speech — bounded under the buffer
  (files: main/voice/voice_client.c, espet-backend/api/voice_router.py,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Telegram replies now spoken + "ringtone" playback diagnostics: every final
  Telegram text reply is also TTS-spoken (dispatcher telegram branch calls
  `voice_client_speak`). Backend gained `sanitize_tts_text()` (strips emoji/pictographs that
  garble edge-tts). `tool_audio_play_wav` logs the WAV header fields + first data bytes to
  diagnose wrong-audio playback from the serial log. Field-verified backend WAVs for the exact
  failing reply are healthy 16 kHz/mono speech (4.3 s, RMS 2177) — device-side playback was
  the suspect (files: main/mimi.c, main/tools/tool_audio.c,
  espet-backend/api/voice_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — TLS AES alloc failure fixed: intermittent "esp-aes: Failed to allocate memory"
  killed LLM calls ("Sorry, I encountered an error") — the HW AES accelerator's on-demand
  internal-RAM DMA buffers fail under post-esp-sr memory pressure. Switched TLS to software
  AES (CONFIG_MBEDTLS_HARDWARE_AES=n); payloads are small, perf impact negligible. Note:
  wake-starvation watchdog errors from the previous fix are gone in field logs; WakeNet
  model lives in flash/model partition via mmap, PSRAM holds buffers — the binding resource
  is internal DIRAM only (files: sdkconfig.defaults.esp32s3, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Wake listener CPU starvation fixed: voice_wake at prio 5 ran the
  read→detect loop flat out (continuous I2S data) and starved IDLE0 — task watchdog fired
  every 5 s, Telegram TLS handshakes failed (esp-aes alloc errors → agent "Sorry, I
  encountered an error"). Lowered to prio 3 and added vTaskDelay(1) per detect frame;
  consumption (32 ms audio per ~10 ms tick) still outpaces the mic. Requires reflash
  (files: main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Voice task stack moved to PSRAM: the 16 KB internal stack kept failing to
  allocate after WiFi/TLS/telegram fragment internal RAM (field log: "failed to create voice
  task" twice, plus esp-aes alloc failures). `voice_client_start()` now uses
  `xTaskCreateStatic` with a 12 KB PSRAM stack
  (new `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y`) and a 6 KB internal-stack dynamic
  fallback (files: main/voice/voice_client.c, sdkconfig.defaults.esp32s3,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Wake-word bring-up fixes from first field flash: (1) voice task stack 16→8 KB
  (16 KB xTaskCreate failed on fragmented internal RAM — the listener never ran); (2)
  `tool_audio_rx_read` gained a LISTEN mode (unarmed reads return live audio to the wake task;
  previously it returned nothing unless a capture was armed, so WakeNet was never fed);
  (3) voice replies now mirror to the last Telegram chat (`telegram_send_to_last_chat`) AND
  are spoken — including the "ESPeting..." working status (files: main/voice/voice_client.c,
  main/tools/tool_audio.c, main/mimi.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Hands-free wake word (Option B, esp-sr WakeNet): GPIO 41 reassigned to the
  camera button (short = snapshot to Telegram, hold ≥600 ms = video; previous white-button
  logic, PTT retired — `MIMI_GPIO_BTN_CAM`, `MIMI_GPIO_BTN_PTT=-1`). Voice is now voice-first:
  `voice_client_start()` loads the WN9 "hiesp" model (`CONFIG_SR_WN_WN9_HIESP`) from a new
  2 MB `model` partition, the `voice_wake` task streams the mic into WakeNet via a new
  exclusive capture API (`tool_audio_capture_arm/rx_read/disarm` replacing the PTT API),
  and on detection plays a "Yes?" ack (TTS WAV pre-cached at boot, beep fallback) then opens
  a command window (1.6 s silence / 10 s cap) feeding the existing transcribe→agent→speak
  pipeline. Listener pauses while the speaker plays (echo control). partitions.csv: apps
  2→3 MB (esp-sr/esp-dl grew the image ~324 KB), spiffs 11.8→7.8 MB (agent files use ~8 KB).
  idf_component.yml: +espressif/esp-sr ^2.0.0. voice_client_on_ptt_stop kept for manual
  triggering only (files: partitions.csv, sdkconfig.defaults.esp32s3, main/idf_component.yml,
  main/mimi_config.h, main/tools/button_handler.{c,h}, main/tools/tool_audio.{c,h},
  main/voice/voice_client.{c,h}, main/mimi.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Voice pipeline end-to-end (STT + TTS): new backend
  `api/voice_router.py` — `POST /api/voice/transcribe` (raw 16 kHz mono PCM →
  faster-whisper local, VAD filter, thread-offloaded; returns `{"text"}`) and
  `POST /api/voice/tts` (`{"text"}` → edge-tts neural voice (default zh-HK-HiuMaanNeural,
  CJK/env configurable) → ffmpeg → 16 kHz mono 16-bit WAV); settings `voice_stt_model`,
  `voice_language`, `voice_tts_voice` in config.py; deps faster-whisper/edge-tts/numpy in
  requirements.txt; endpoints live-verified (STT round-trip exact, 401 on bad key).
  Firmware: `tool_audio_play_wav()` (RIFF chunk-walk parser, slot-duplicated TX streaming)
  + `tool_audio_beep()` in tool_audio; new `voice/voice_client.c` (PSRAM HTTP client:
  PTT release → beep → POST PCM → transcript → inbound bus as system:"voice" message →
  agent reply → outbound dispatcher calls `voice_client_speak()` → TTS fetch → WAV playback);
  wired in mimi.c (PTT release hook, system-channel speak) + CMake SRCS; removed
  `audio_mictest` diagnostic tool. Known limits: re-pressing PTT during a pipeline run can
  overwrite the PCM buffer mid-upload; voice flow requires the backend reachable (files:
  espet-backend/api/voice_router.py, espet-backend/main.py, espet-backend/config.py,
  espet-backend/requirements.txt, main/tools/tool_audio.c, main/tools/tool_audio.h,
  main/voice/voice_client.c, main/voice/voice_client.h, main/mimi.c, main/CMakeLists.txt,
  main/tools/tool_registry.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Added `audio_mictest` mic quality diagnostic (quiet 3 s baseline vs 3 s speak
  window, differential RMS verdict) with capture-health checks for the field-observed noise issue:
  stuck-sample ratio (0xFFFF wiring signature — L/R not grounded / SD marginal), clipping in the
  quiet window, and DC offset; verdict states STT-readiness. Registered in tool_registry (files:
  main/tools/tool_audio.c, main/tools/tool_audio.h, main/tools/tool_registry.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Speaker bring-up confirmed working in the field (fault was DIN wiring on GPIO 21).
  Removed the bring-up test tools `audio_selftest` and `audio_maxtest` (plus Goertzel/pad-probe/
  loopback helpers and selftest constants) from tool_audio; kept the production surface:
  full-duplex I2S init, `audio_play_tone` (freq/duration/volume), `audio_record_test`, green-button
  PTT capture (files: main/tools/tool_audio.c, main/tools/tool_audio.h, main/tools/tool_registry.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Added `audio_maxtest` deep MAX98357A diagnostic tool (amp has no readback, so it
  tests 3 layers): (1) GPIO pad-toggle probes on BCLK/WS (static = driver/wire fault) and DIN at
  idle-vs-live (static live = DIN wire on wrong pin, e.g. old GPIO 2), (2) three full-volume
  listen tones (440/1k/2k Hz), (3) Goertzel narrowband 1 kHz loopback — compares the mic's 1 kHz
  bin at baseline vs during the tone (ratio ≥3×), which detects the speaker even through the
  ~7000-RMS mic noise floor where the RMS loopback fails. Registered in tool_registry (files:
  main/tools/tool_audio.c, main/tools/tool_audio.h, main/tools/tool_registry.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Audio selftest hardening: field test showed LOOPBACK false-pass (mic ambient
  floor RMS ~11000 dwarfed the fixed 500 threshold). Reworked part 3 as a differential test —
  800 ms silent baseline vs 2 s full-volume 1 kHz tone; pass requires tone RMS > 2× baseline
  AND > +500 delta. Failure path now prints a MAX98357A-specific checklist (SD/SD_MODE pin
  shutdown, DIN pin-move from old GPIO 2, VIN 5V, speaker terminals) (files:
  main/tools/tool_audio.c, main/tools/tool_audio.h, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Audio bring-up: rewrote `tool_audio.c` from RX-only mic stub into full-duplex
  I2S (TX+RX pair on I2S_NUM_0 sharing BCLK 38/WS 39, both channels enabled from init with
  `auto_clear` silence). Implemented MAX98357A speaker playback (`audio_play_tone` with
  freq/duration/volume params), added `audio_selftest` tool (3-part: 3 s mic RMS verdict +
  440/880 Hz audible tones + 2 s acoustic loopback where the mic must hear the 1 kHz speaker
  tone — proves the whole output chain), registered in tool_registry (files:
  main/tools/tool_audio.c, main/tools/tool_audio.h, main/tools/tool_registry.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Reduced Telegram poll timeout from 30s to 5s (MIMI_TG_POLL_TIMEOUT_S), ensured empty secrets take precedence over NVS proxy to avoid phantom proxy deadlocks, and enabled comprehensive Telegram/proxy diagnostics logging (files: main/mimi_config.h, main/proxy/http_proxy.c, main/channels/telegram/telegram_bot.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Fixed Telegram bot getUpdates response truncation and memory starvation: moved HTTP response buffers to PSRAM (heap_caps_calloc/heap_caps_realloc), expanded esp_http_client RX/TX buffer sizes to 4096 bytes, added limit=5 query parameter to getUpdates to page messages safely, and flushed stale 15-message Telegram backlog (files: main/channels/telegram/telegram_bot.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-08 — Configured cloud LLM endpoint for agnes-2.5-flash (https://apihub.agnes-ai.com/v1/chat/completions) via MIMI_SECRET_OPENAI_URL, disabled Wi-Fi modem sleep (esp_wifi_set_ps(WIFI_PS_NONE)) in wifi_manager and wifi_onboard to eliminate BEACON_TIMEOUT disconnections, and added Wi-Fi connection check to telegram_poll_task (files: main/mimi_secrets.h, main/mimi_config.h, main/wifi/wifi_manager.c, main/onboard/wifi_onboard.c, main/channels/telegram/telegram_bot.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-08 — Updated hardware pin assignments to new physical wiring: OLED SCL/SDA -> GPIO 2/1, Green Button -> GPIO 41, White LED -> GPIO 42, Audio BCLK/WS -> GPIO 38/39, Amp DIN -> GPIO 21, Mic SD -> GPIO 40, White Button unassigned (files: main/mimi_config.h, main/tools/tool_oled.c, main/tools/button_handler.h, main/tools/button_handler.c, main/tools/tool_audio.c, main/agent/context_builder.c, main/tools/tool_registry.c, spiffs_data/config/SOUL.md, spiffs_data/skills/gpio-control.md, espet-backend/config_store.py, .agent/ESPET_PINS.MD, AGENTS.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — **Frontend redesign (this session, frontend-only):** replaced the
  vanilla `app.js`/`style.css`/anime.js dashboard with a **React 19 + antd 6 +
  @lobehub/ui** SPA in `espet-backend/src/` (esbuild → `static/vendor/app/`,
  code-split, committed, cache-busted). New glass **top nav** (desktop) /
  **bottom dock** (<820px), animated gradient orbs, dark theme. All 10 views
  ported incl. a new **Chat** view (direct browser→ESP32 WS gateway :18789,
  localStorage transcript) and **Home** onboarding checklist + lobe-charts.
  Personality/Memory use lobe-editor; Skills use exact-byte source + preview.
  **No backend/firmware code changed** — same endpoints. Fixes: used antd-style
  ThemeProvider (lobe-ui's throws), gated editor onChange to stop false-dirty.
  Rewrote `.agent/ESPET_PINS.MD` to the NEW pin map (OLED 2/1, green btn 41,
  LED 42, I2S 38/39, mic 40, speaker 21; white button removed). Deferred
  backend/firmware work (SQLite, metrics endpoint, chat relay, firmware pin remap)
  to `.agent/NEXT_PLAN_BACKEND.MD`. Verified all views + mobile + dirty-guard via
  Playwright, 0 console errors (files: espet-backend/src/**, espet-backend/
  static/index.html, espet-backend/package.json, tools/build-app.mjs,
  tsconfig.json, .agent/ESPET_PINS.MD, .agent/NEXT_PLAN_BACKEND.MD,
  .agent/SYSTEM_OVERVIEW.md; deleted static/app.js, static/style.css,
  static/vendor/anime.iife.min.js).

- 2026-09-10 — **Dim-blue accent family + control height:** global
  `controlHeight: 40` (inputs/selects now match the 40px buttons — fixes the
  misaligned `http://` addon vs Save in Camera/Device rows); checked `Switch`
  gets the dim-blue gradient (global CSS), selected `Segmented` items and
  solid `Radio.Button` (Schedule modal Trigger) get
  `rgba(147,197,253,0.22)`/`#dbeafe` component tokens (was grey bg + white
  text), `::selection` dim blue. DESIGN.MD §1/§2/§4/§6 updated (files:
  espet-backend/src/theme.tsx, src/global.css, DESIGN.MD,
  .agent/SYSTEM_OVERVIEW.md).

- 2026-09-10 — Top nav pill now fits its content and centers itself
  (`width: fit-content` + `margin-inline: auto` on the fixed bar; nav flex
  `none`) — no more dead space on the right since the status chips moved to
  the bottom-right dock; UnsavedChip still widens it transiently. DESIGN.MD §4
  updated (files: espet-backend/src/components/GlassNav.tsx, DESIGN.MD).

- 2026-09-10 — **Status/design polish pass (frontend-only):** (1) semantic
  status colors — `colorSuccess/colorError/colorWarning` now dimmed mint
  `#8fd9ae` / red `#eda7a7` / yellow `#e6cf94` (general stays glass neutral);
  new `StatusChip` + `SectionDirtyChip` primitives in StatusChips.tsx, `ui.tsx`
  Badge tones remapped to `default/success/warning/danger`, toasts gained an
  `"info"` kind, all status text Title Case. (2) Chat header avatar → brand
  logo (white-on-light invisible bot icon fixed; dark icon color on transcript
  avatars). (3) Schedule modal switch labels spaced. (4) Modal-open scroll
  jump fixed — viewport scroll moved to `html` (`overflow-y: auto`,
  `#root min-height: 100%`) so antd's body scroll-lock can't reset it. (5)
  "Unsaved Changes" moved from nav to the edited container's card header
  (soul/profile/memory/device). (6) Markdown editing unified on `SourceField`
  (Edit|Preview tabs + label prop + `canonical()`); `MarkdownField.tsx`
  deleted. DESIGN.MD §1/§2/§3/§4/§6 rewritten (files: espet-backend/src/
  theme.tsx, store.tsx, ui.tsx, global.css, components/StatusChips.tsx,
  components/GlassNav.tsx, components/SourceField.tsx, deleted components/
  MarkdownField.tsx, views/{Chat,Camera,Home,Agent,Device,Schedule,Skills,
  Personality,Memory,Profile}.tsx, DESIGN.MD, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-10 — Palette fully de-blued: the slate text ramp (blue-tinted
  grays) replaced with pure neutral grays across all of src (text #f5f5f5 /
  #a3a3a3 / #737373, bg #050505, orbs/scrollbars/Chart palette neutralized);
  Agent's "manual overrides active" antd `Tag color="blue"` → neutral; boot
  splash glow + theme-color switched blue/emerald → white; footer stripped to
  plain text (no border/background). DESIGN.MD token tables updated to match
  (files: espet-backend/src/**, static/index.html, DESIGN.MD).
- 2026-09-10 — Design standardization: **DESIGN.MD rewritten** as the single
  source of truth for the monochrome token system (canvas/glass/text ramp/
  brightness-based tones/dim-blue primary/typography/layout/motion/component
  rules) with a new mandatory **Design Token Rule in AGENTS.md** (every frontend
  UI change must follow + update DESIGN.MD); persistent status chips (config
  version, backend health) moved from the nav's right side into a fixed
  **bottom-right StatusDock** (desktop only; mobile keeps the compact row in
  the top pill; UnsavedChip pulse stays in the nav); footer redesigned to
  neutral monochrome (hairline top border, slate/gray text — was the last
  blue-tinted #475569 element) (files: DESIGN.MD, AGENTS.md,
  espet-backend/src/components/StatusChips.tsx, GlassNav.tsx, src/App.tsx,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-09-09 — Monochrome redesign + Camera page: whole system switched to
  black + white (canvas #050507, white floating orbs, dimmed-white glass
  overlays, brightness-based tones; primary buttons = the only accent, very dim
  light blue; antd tokens + charts + nav/rail/chips/bubbles all desaturated);
  fonts — Outfit for all rendered content incl. markdown previews, JetBrains
  Mono only for raw markdown sources + technical values (dropped
  `.espet-md-mono`/`.espet-editor-surface` rules); new **Camera** view polling
  `http://<device>:18789/camera.jpg` with rate Segmented, Snapshot, LIVE tag,
  shared device address, graceful offline state (firmware route = NEXT_PLAN
  §6); camera icon added (files: espet-backend/src/global.css, theme.tsx,
  ui.tsx, components/GlassNav.tsx, SectionRail.tsx, StatusChips.tsx,
  MarkdownField.tsx, SourceField.tsx, views/Camera.tsx, Home.tsx, Chat.tsx,
  Agent.tsx, Skills.tsx, routes.ts, App.tsx, components/Icon.tsx,
  .agent/NEXT_PLAN_BACKEND.MD).
- 2026-09-09 — Assistant/System nav rework: dropdowns and mobile bottom-sheets
  removed; Assistant is one scroll page (Personality → Profile → Memory →
  Schedule → Skills) and System another (Device → Tools), reusing the existing
  view components unchanged. New `SectionRail` — right-side glass bubbles on a
  vertical blue→mint line with scroll-spy active state (label pills on desktop,
  icon-only circles <820px), tap-to-scroll + `#assistant/<section>` deep links,
  bare `#memory`-style hashes alias to page+section. Fixed a latent bug found
  along the way: `html,body { overflow-x: hidden }` made body a scroll
  container so `window.scrollY` never moved (nav scroll-shrink and the rail
  spy were dead); switched to `overflow-x: clip`. Routes gained
  `parseRoute({view,section})`; App holds `{view, section}` state (files:
  espet-backend/src/routes.ts, components/SectionRail.tsx,
  views/AssistantPage.tsx, views/SystemPage.tsx, components/GlassNav.tsx,
  src/App.tsx, src/global.css, src/views/Home.tsx).
- 2026-09-09 — Design follow-up pass: bigger brand logo (46 px nav / 128 px
  boot splash); fixed Assistant/System dropdowns (NavItem now forwards antd
  Dropdown trigger props to the DOM node); roomier status chips + antd Button
  tokens (paddingInline 18, controlHeight 40); **Edit mode now shows raw
  markdown source** in a mono textarea everywhere (Lexical WYSIWYG dropped from
  MarkdownField — `@lobehub/editor` no longer imported, bundle 16.0→15.5 MB);
  **all UI wording renamed MimiClaw → ESPET** (views, routes, DEFAULT_SOUL,
  EMPTY_MEMORY, config_store defaults, seeded data/config.json, spiffs_data
  SOUL/MEMORY/skill-creator) with MimiClaw kept only as the shell footer credit
  "powered by MimiClaw" (files: espet-backend/src/components/GlassNav.tsx,
  MarkdownField.tsx, StatusChips.tsx, src/global.css, src/theme.tsx,
  src/App.tsx, src/views/*.tsx, src/routes.ts, static/index.html,
  espet-backend/config_store.py, spiffs_data/config/SOUL.md,
  spiffs_data/memory/MEMORY.md, spiffs_data/skills/skill-creator.md).
- 2026-09-09 — Dashboard design pass: brand logo (`static/brand/espet_logo.jpg`
  from the FYP brand folder) in nav/boot splash/favicon; gradient orbs now float
  full-viewport (4 orbs, multi-waypoint keyframes); primary buttons painted with
  the blue→mint brand gradient; nav status chips use antd `Badge status` dots
  (processing pulse = unsaved); all .md surfaces render in JetBrains Mono
  (`fontFamilyCode` + `.espet-md-mono`); every markdown input gained View/Edit
  modes (Personality/Memory Segmented toggle, Skills Edit/Preview tabs, Profile
  Fields/Markdown tabs with two-way conversion); top nav became a floating glass
  pill with `motion` spring entrance, scroll shrink, and layoutId magic-move
  active pill (files: espet-backend/src/components/GlassNav.tsx,
  StatusChips.tsx, MarkdownField.tsx, SourceField.tsx, GradientBackground.tsx,
  src/global.css, src/theme.tsx, src/views/Profile.tsx, src/views/Chat.tsx,
  static/index.html, static/brand/espet_logo.jpg).
- 2026-09-08 — Added skills management end-to-end: web CRUD (`/api/ui/skills`
  + Skills dashboard view with editor modal), device sync (`GET /api/skills/sync`
  pulled only when `versions.skills` advances; filename-based ownership in
  `sync_state.json` protects agent-created and flashed skills), backend seeds
  the 4 default skills, skills prompt buffer 2048→3072 bytes. Also added
  `.agent/HERMAN_ROADMAP.MD` (proactive-half gap analysis + prioritized
  roadmap) (files: espet-backend/config_store.py, espet-backend/api/config_router.py,
  main/sync/config_sync.c, main/agent/context_builder.c,
  espet-backend/static/app.js, espet-backend/static/index.html,
  .agent/HERMAN_ROADMAP.MD, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-07 — Added `npm run dev:win` / `npm run dev:mac` launch scripts to
  `espet-backend/package.json` (Windows: venv python + uvicorn --reload :8000;
  macOS: source venv/bin/activate + python main.py); run from `espet-backend/`
  (files: espet-backend/package.json, .agent/SYSTEM_OVERVIEW.md).
- 2026-09-07 — Dashboard layout fix: `.main` now centers horizontally next to the
  sidebar (`margin-inline: auto`; was flush-left leaving dead space) and cache
  version bumped to style.css?v=8. Added DESIGN.MD documenting the dashboard
  design system (tokens, components, layout, motion, a11y) (files:
  espet-backend/static/style.css, espet-backend/static/index.html, DESIGN.MD).
- 2026-09-07 — Added session memory rule: every feature/new implementation must
  auto-update this file in the same change (log entry + touched sections);
  rule added to AGENTS.md (files: AGENTS.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-07 — Phantom camera-snap hardening: the 2026-10-06 pulse-width fix
  was still insufficient — saying "Hi ESP" snapped a photo because the coupled
  pulse outlasts MIN_PRESS_MS 50. Camera button (GPIO 41) press acceptance now
  also requires (a) speaker not busy (`tool_audio_is_busy()`), (b) at least
  200 ms after playback ends (AUDIO_GLITCH_COOLDOWN_MS — the coupled pulse can
  outlast the burst), and (c) the live pin still LOW when button_task runs
  (transient rejection). MIN_PRESS_MS raised 50 → 80 ms (files:
  main/tools/button_handler.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-07 — Real video capture for the camera button: long-press previously
  ran a mock task that captured frames and dropped them (nothing was ever sent
  to Telegram). `tool_camera.c` now records JPEG frames into a 4 MB PSRAM
  store at ~10 fps (300-frame / 4 MB auto-stop), assembles an in-place MJPEG
  AVI container (constant 224-byte header written last, idx1 appended after
  the last chunk — no copies), and returns it via
  `tool_camera_video_stop_and_get()` / `tool_camera_video_release()`. New
  `telegram_send_video[_to_last_chat]()` uploads via sendVideo multipart
  (video/avi, supports_streaming, 60 s timeout, 4 KB chunked writes). mimi.c
  long-press release path: stop → assemble → send → release (files:
  main/tools/tool_camera.c, main/tools/tool_camera.h,
  main/channels/telegram/telegram_bot.c, main/channels/telegram/telegram_bot.h,
  main/mimi.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-07 — Video task creation fix: first field test showed
  "Failed to create video task" — the internal heap is too fragmented after
  WiFi/TLS/Telegram come up to hand out a contiguous 4 KB task stack (same
  root cause as the mDNS task start failure). video_task now uses a
  PSRAM-allocated stack via xTaskCreateStatic (3 KB internal-stack
  xTaskCreate fallback), matching the voice_task pattern (files:
  main/tools/tool_camera.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-07 — Video send fixes: (1) PSRAM leak — tool_camera_video_stop_and_get
  transferred the 4.1 MB AVI store by nulling its internal pointer, so
  tool_camera_video_release() freed nothing; ownership now stays inside
  tool_camera (release() frees the store, start() refuses to run while an
  unreleased capture exists). (2) "no active Telegram chat ID yet" skip on the
  first button press after a reboot — s_last_chat_id is now persisted to NVS
  (tg_config/last_chat, written only on change) and restored at bot init.
  (3) photo/video to_last_chat wrappers return ESP_ERR_INVALID_STATE instead
  of fake ESP_OK on skip; mimi.c logs a clear hint (files:
  main/tools/tool_camera.c, main/channels/telegram/telegram_bot.c, main/mimi.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-10-07 — OLED hunt removal + public DNS fallback: the display driver
  hunted 25 SCL×SDA pin pairs on EVERY display update (25-pair I2C scan per
  status change) and its candidate list included GPIO 41 (camera button) and
  42 (LED) — pulsing them caused phantom button edges and ~1 s stalls.
  display_status.c now probes only the authoritative pins from
  .agent/ESPET_PINS.MD (SCL=2, SDA=1) once; on NACK (panel dead/unwired) the
  display is disabled until reboot with a single clear error, and 5
  consecutive refresh failures disable it too. wifi_manager.c sets public DNS
  fallbacks (8.8.8.8, 1.1.1.1) on the STA netif at GOT_IP — when the
  DHCP-provided router DNS is unresponsive, lwIP retries these instead of
  failing all TLS connects (field-observed outage windows) (files:
  main/display/display_status.c, main/wifi/wifi_manager.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-10-07 — Outbound Telegram retry: during network outage windows the
  outbound dispatcher dropped replies after a single failed send (field log:
  a fully-processed reply was lost). Telegram sends in the outbound task now
  retry 6× at 5 s intervals (~30 s resilience) before giving up; voice speak
  still only fires on final success (files: main/mimi.c,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-10-07 — lwIP socket starvation fix (root cause of flappy
  "network outages"): device logs showed `esp-tls: Failed to create socket`
  and failures against the LOCAL backend too — not the router/ISP. Arithmetic:
  bot opens a fresh TLS connection per ~6 s poll, each closed client socket
  sits in TIME_WAIT for 2*MSL; defaults needed ~20 TIME_WAIT slots
  (120 s each) against CONFIG_LWIP_MAX_SOCKETS=10 → all new connections
  (LAN + WAN) stalled until slots aged out, then recovered — matching every
  observed outage window. Raised CONFIG_LWIP_MAX_SOCKETS 10 → 16 and
  CONFIG_LWIP_TCP_MSL 60000 → 10000 (TIME_WAIT ≈ 20 s) in sdkconfig +
  sdkconfig.defaults.esp32s3; fullclean rebuild. Voice pipeline ("Hi ESP"
  wake word, mic feeder, TTS/STT) DISABLED for field debug at the user's
  request — voice_client_start() and voice_client_speak() call sites commented
  in mimi.c (esp-sr is a large internal-RAM consumer and TTS held sockets for
  30 s); re-enable via those markers once stable (files: sdkconfig,
  sdkconfig.defaults.esp32s3, main/mimi.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-07 — Added docs/ARCH_COMPARISON_DIRECT_VS_CLOUD.md: design study
  comparing Plan A (current direct-connect: device polls Telegram + calls LLM
  itself, LAN backend for tools) vs Plan C (cloud gateway: single persistent
  device WSS connection to a hosted server running the Telegram gateway, LLM
  orchestration, and registry) — data flows, ASCII diagrams, adv/disadv,
  side-by-side table, and a firmware/backend migration map for productization
  (files: docs/ARCH_COMPARISON_DIRECT_VS_CLOUD.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-07 — Hybrid mode implemented (branch `newplan`, Plan C-Hybrid slice,
  docs/HYBRID_MODE_PLAN.md): the backend gains a cloud-gateway role —
  `services/device_registry.py` (device connections + chat bindings, JSON
  persistence), `services/telegram_gateway.py` (single getUpdates poller,
  routes updates to the owning device, send_text/send_photo), and
  `api/cloud_router.py` (WS `/api/cloud/ws/device` with device-key auth,
  hello handshake, message_in/message_out/photo_out frames, binding REST).
  Gated by `CLOUD_GATEWAY_ENABLED`/`TELEGRAM_BOT_TOKEN` in settings.
  Firmware: NEW `main/channels/cloud_link/` (esp_websocket_client, one
  persistent outbound WSS to /ws/device, hello/hello_ack, frame reassembly,
  message_in → bus channel "cloud"); `MIMI_CHAN_CLOUD` added to message_bus;
  when `MIMI_CLOUD_LINK_ENABLE=1` (default) mimi.c starts cloud_link instead
  of telegram polling and the outbound dispatcher sends "cloud" replies with
  6×5 s retries. Agent loop/memory/tools unchanged. Host E2E tests
  (espet-backend/tests/test_cloud_gateway.py, mock Telegram HTTP server +
  real WS device client): 8/8 passed (auth, handshake, inbound routing,
  outbound send, auto-bind, persistence, disconnect safety, photo). Hardware
  build/flash + TC-09 deferred by request (files: espet-backend/services/*,
  espet-backend/api/cloud_router.py, espet-backend/main.py,
  espet-backend/config.py, espet-backend/tests/test_cloud_gateway.py,
  main/channels/cloud_link/*, main/bus/message_bus.h, main/mimi_config.h,
  main/CMakeLists.txt, main/mimi.c, docs/HYBRID_MODE_PLAN.md,
  .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Fixed firmware compile errors and WebSocket 403 handshake in hybrid mode:
  removed non-existent `esp_websocket_client_config_t.buffer_size_tx`, corrected event
  registration API to `esp_websocket_register_events` (was `esp_websocket_client_register_events`),
  included `esp_crt_bundle.h` for `crt_bundle_attach`, updated firmware WS URL path to
  `/api/cloud/ws/device`, and added `/ws/device` alias in `espet-backend/main.py`
  (files: main/channels/cloud_link/cloud_link.c, espet-backend/main.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Fixed hybrid mode Telegram gateway dispatch and reconnection:
  made `ws_device` frame loop in `api/cloud_router.py` query `get_gateway()` dynamically
  so runtime gateway restarts apply to existing client sockets, enabled `enable_close_reconnect`
  in `cloud_link.c` so the ESP32 survives backend restarts, and added bootstrap env keys to `.env`
  (files: espet-backend/api/cloud_router.py, main/channels/cloud_link/cloud_link.c, espet-backend/.env, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Added docs/VOICE_AUDIO_STATUS.md: detailed audit document in Chinese
  listing all commented-out and active microphone, speaker, TTS, and STT code locations,
  field-debug context, and recovery steps (files: docs/VOICE_AUDIO_STATUS.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Upgraded hybrid Telegram gateway to 30s seamless long-polling with 0s idle rest delay; removed redundant poll interval UI field from cloud dashboard (files: espet-backend/services/telegram_gateway.py, espet-backend/api/cloud_router.py, espet-backend/config.py, espet-backend/static/cloud.html, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Restored Telegram reply TTS audio output in mimi.c (both direct and cloud-link channels); purged obsolete PTT (Push-To-Talk) button code and configs from button_handler and voice_client, retaining single GPIO 41 button strictly for camera snap and video capture; updated docs/VOICE_AUDIO_STATUS.md (files: main/mimi.c, main/tools/button_handler.h, main/tools/button_handler.c, main/mimi_config.h, main/voice/voice_client.h, main/voice/voice_client.c, docs/VOICE_AUDIO_STATUS.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Re-enabled hands-free wake word listener (voice_client_start) in mimi.c; completely decoupled voice replies from Telegram (voice chat responses go directly to MAX98357A speaker with zero Telegram traffic); updated docs/VOICE_AUDIO_STATUS.md (files: main/mimi.c, main/voice/voice_client.c, docs/VOICE_AUDIO_STATUS.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Fixed wake word diagnostics and I2S RX arbitration: moved 5s WakeNet debug report to the top of voice_task loop in voice_client.c so frame/feeder stats output unconditionally; added rate-limited warning logging for feeder DMA read failures in tool_audio.c and paused mic_feeder_task during standalone s_recording sessions to prevent I2S RX contention (files: main/voice/voice_client.c, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Fixed mic_feeder_task DMA read starvation: reduced feeder read buffer from oversized 16 KB (4096 samples) to AUDIO_READ_CHUNK_BYTES (2048 bytes / 256 frames / 16 ms) with 500ms timeout matching proven capture_task, preventing persistent 200ms DMA timeouts; preserved partial reads (files: main/tools/tool_audio.c, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Boosted wake word sensitivity: applied 2.5x digital gain (+8 dB) to INMP441 samples in mic_feeder_task, switched WakeNet to DET_MODE_95 (aggressive detection), and lowered detection threshold from 0.63 to 0.45 (files: main/tools/tool_audio.c, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Added direct STT testing in audio_record_test: exposed voice_client_transcribe and wired audio_record_test to send captured PCM directly to /api/voice/transcribe and print recognized speech in serial terminal (files: main/voice/voice_client.h, main/voice/voice_client.c, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Implemented Option 3 Software Wake Word pipeline with custom wake words ("Mimi" / "Hey Mimi" / "米米"): replaced rigid on-chip "Hi ESP" WakeNet with low-overhead energy VAD (RMS > 950 for 2 frames) and 250ms pre-roll capture on ESP32; added backend wake word verification filter (`check_wake_word`, `voice_wake_words` in config.py) in `/api/voice/transcribe`; supported both one-shot commands ("Hey Mimi turn on LED" -> direct ReAct execution) and two-step wake ("Mimi" -> "Yes?" ack -> follow-up command); ambient talk without wake word is silently ignored; updated idle OLED message to "Say Mimi to wake" (files: main/voice/voice_client.c, main/voice/voice_client.h, main/tools/tool_audio.c, main/tools/tool_audio.h, main/display/display_status.c, espet-backend/config.py, espet-backend/api/voice_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Fixed mic_feeder_task starvation during utterance capture and added "Hey Expert" wake word: fixed feeder task falsely parking during armed recording by replacing s_recording with s_capture_task_handle check; calibrated VAD threshold to RMS 420 based on actual room floor (210 RMS) and speech peak (750 RMS); added "expert", "hey expert", "hi expert" to config.py and Whisper prompt (files: main/tools/tool_audio.c, main/voice/voice_client.c, main/display/display_status.c, espet-backend/config.py, espet-backend/api/voice_router.py, espet-backend/.env, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Optimized voice response latency: reduced trailing silence cutoff threshold CMD_SILENCE_MS from 1200ms to 700ms in voice_client.c, cutting 500ms of post-speech waiting delay (files: main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-08 — Implemented half-duplex turn-taking with paused mic during LLM reasoning and TTS playback: once a command is queued to the agent, voice client marks s_busy=true and suspends mic VAD/ring reads, preventing self-triggering from speaker audio, avoiding Wi-Fi packet contention during tool execution, and saving backend Whisper compute; voice_client_speak clears s_busy and flushes ring buffer upon playback completion (files: main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-09 — Optimized voice listening latency and robustness: reduced wake utterance cap from 8.0s to 1.5s (UTTERANCE_MAX_MS=1500) and command cap from 10.0s to 4.5s (CMD_MAX_MS=4500); added adaptive dynamic silence threshold tracking (silence_cutoff = s_ambient_noise_rms + 80) in voice_client.c; implemented single-sample rail pop deglitcher in mic_feeder_task (tool_audio.c) to suppress electrical I2S full-scale spikes (files: main/voice/voice_client.c, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-09 — Fixed audio cache persistence and spurious wake re-triggering: implemented `tool_audio_clear_cache()` draining both hardware I2S DMA FIFO and software ring buffer; added explicit cache clearing and 150ms/250ms acoustic settle cooldowns after playing 'Yes?' and after agent TTS replies; in `voice_run_command`, isolated pure wake word detections (e.g. repeated '米米') from real commands, discarding empty commands instead of falsely sending '米米' to the agent LLM loop (files: main/tools/tool_audio.h, main/tools/tool_audio.c, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-09 — Fixed premature command cutoff after 'Yes?' ack: removed aggressive 700ms silence timeout that immediately aborted listening; ensured the full 4.5s command window strictly begins after 'Yes?' finishes; added guaranteed 2.5s minimum speaking grace period before trailing silence (1200ms) can complete, and updated OLED to indicate active command listening (files: main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-09 — Fixed empty Google MCP servers card on Home dashboard: replaced fragile DonutChart (whose Recharts ResponsiveContainer collapsed to 0 width inside display:grid placeItems:center) with a clear list of connected servers displaying names, status dots, and tool counts (Gmail, Calendar, Drive, Docs) with success badge tone (files: espet-backend/src/views/Home.tsx, espet-backend/static/vendor/app/chunks/Home-WQ7WBSGL.js, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-09 — Fixed OLED electrical interference causing continuous mic false triggers: SSD1306 internal charge pump (300-450 kHz) and shared 3.3V/GND breadboard rails induced large DC bias/ripple (~8500 raw RMS in quiet room), trapping VAD in a 24/7 capture-upload loop that made the mic seem insensitive to user speech; implemented a 1st-order DC-blocking IIR high-pass filter (fc ≈ 20Hz) in `mic_feeder_task` (`tool_audio.c`) to strip DC offsets/ground drift, and implemented `calc_ac_rms()` (zero-mean AC RMS) in `voice_client.c` for VAD listening, command trailing silence, and utterance capture so pure DC rail shifts can never trigger speech detection (files: main/tools/tool_audio.c, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-09 — Added Cloud tab to top navigation bar beside Agent and added shared GlassNav bar with bidirectional routing to /cloud: updated `PRIMARY` routes order (`routes.ts` & `chunk-UXN76GPX.js`), added routing interception in `index.js` to redirect clicks to `/cloud`, and added floating glass navbar with active Cloud pill and cross-view routes (Home, Agent, Chat, Camera, Assistant, System) in `cloud.html` (files: espet-backend/src/routes.ts, espet-backend/static/cloud.html, espet-backend/static/index.html, espet-backend/static/vendor/app/index.js, espet-backend/static/vendor/app/chunks/chunk-UXN76GPX.js, .agent/SYSTEM_OVERVIEW.md).


- 2026-10-10 — Fixed endless self-wake loop (device re-woke with nobody speaking): root cause was TTS echo/noise > fixed VAD threshold → capture → Whisper (biased by wake words in `initial_prompt`) hallucinating "米米" → unconditional wake match → "Yes?" → loop. Backend: removed wake words from `initial_prompt` (now neutral/empty `voice_stt_prompt`), added pre-STT RMS gate (`voice_min_rms` 250), per-segment confidence gate (`voice_min_avg_logprob` −1.1 / `voice_max_no_speech_prob` 0.75, values calibrated against 19 real utterances vs 15 hallucination cases), and repeated-wake-word stripping in `check_wake_word` ("米米 米米" → wake-only, no command). Firmware: 700 ms post-playback echo guard (`ECHO_GUARD_MS`) replacing the 250 ms cooldown, 450 ms ack settle before the command window (`ACK_SETTLE_MS`), adaptive VAD threshold `max(420, 1.33×ambient)` capped at 14000 with a min-statistics floor tracker that runs on every frame (converges in a loud room where all frames exceed the old fixed threshold), trigger raised from 2 to 3 consecutive frames, and adaptive trailing-silence cutoff (files: espet-backend/api/voice_router.py, espet-backend/config.py, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-10 — Fixed VAD noise floor never converging + stale-backend-IP link death (field log after the previous voice fix): (1) the ambient-floor estimator lived in voice_task, which is blocked for 1.5 s + seconds of STT on every capture, so it crawled 420→1068 over two minutes while the room sat at ~9000 RMS and false-triggered forever; moved estimation into the always-running mic_feeder_task as a ~4 s sliding-window minimum of per-read RMS (stationary room noise vs transient speech ⇒ min locks onto the floor, converges on the first read, and cannot be dragged up mid-utterance) exposed as `s_mic_floor`, consumed by voice_client's `vad_trigger_threshold()`. (2) `cloud_link_start()` ran before mDNS discovery and built the WebSocket from the stale compiled-in IP; the WS client reconnects to a FIXED URI, so it retried the dead address forever (TLS-timeout storm starving Core 0 and stalling STT to 17-21 s). Added a cloud supervisor task that rebuilds the client whenever the discovered URL changes. (3) the boot-time `tool_registry_fetch_remote_tools()` hit the same stale IP and never retried, leaving 0 remote tools; added a bounded background retry task (re-fetch is idempotent). (files: main/tools/tool_audio.c, main/voice/voice_client.c, main/channels/cloud_link/cloud_link.c, main/mimi.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-10-11 — Fixed wake word recognition failures on "Hey Mimi": resolved Whisper punctuation mismatch in check_wake_word (commas/periods in "Hey, Mimi." previously caused 100% regex match failure); added fallback to vad_filter=False when Silero VAD discards short speech segments; added mapping for Japanese Whisper phonetic transcriptions ("耳"/"にみ" -> "mimi"); added anti-repetition limits (repetition_penalty=1.2, no_repeat_ngram_size=3, max_new_tokens=96) to prevent 20s token generation hangs on ambient noise; saved incoming captures to data/last_capture.wav with detailed RMS/length logging; increased firmware wake utterance capture cap from 1.5s to 2.5s (UTTERANCE_MAX_MS=2500) to prevent premature cutoffs; and fixed `mic_feeder_task` premature loop break on ring buffer full that zeroed `sumsq` and poisoned `s_mic_floor` to 0, trapping VAD in a permanent false-triggering loop on ambient OLED/breadboard electrical noise (files: espet-backend/api/voice_router.py, main/voice/voice_client.c, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-06 — Resolved TTS 403 failure, changed idle prompt to "Say Hey Mimi", and eliminated VAD continuous false-trigger loop: upgraded `edge-tts` to 7.3.1 fixing Microsoft Bing 403 Forbidden endpoint deprecation; updated `display_status_idle()` to "Say Hey Mimi" and prioritized "mimi,hey mimi" in wake word configurations; removed obsolete +8 dB (2.5x) digital mic gain in `mic_feeder_task` and `capture_task` that caused rail-to-rail audio clipping (28000+ RMS); clamped DC filter `s_dc_y_prev` accumulator to prevent numeric blowup; added auto-detection/switch for INMP441 Left vs Right slot if L/R pin is floating/high; and guarded `vad_trigger_threshold` against perpetual loops when floor exceeds `VAD_RMS_THRESHOLD_MAX` (files: espet-backend/requirements.txt, espet-backend/config.py, espet-backend/.env, main/display/display_status.c, main/tools/tool_audio.c, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-06 — Removed the #agent 3D companion tab and avatar customizer from the dashboard navigation: removed agent from primary route declarations, unmounted agent view from SPA router with graceful fallback to #overview, eliminated toy.bundle.js from index.html, removed /#agent link from cloud.html, and bumped cache-buster token (files: espet-backend/src/routes.ts, espet-backend/src/App.tsx, espet-backend/static/cloud.html, espet-backend/static/index.html, espet-backend/static/vendor/app/index.js, espet-backend/static/vendor/app/chunks/chunk-UXN76GPX.js, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-06 — Diagnosed and fixed Telegram bot non-responsiveness and multi-developer LAN mDNS collision: (1) identified that ESP32 was connecting to another teammate's machine (`192.168.0.231`) on the same LAN due to `resolve_once` querying mDNS with `max_results=1`; updated `backend_discovery.c` to query up to 8 instances and prioritize matching the compiled-in `BACKEND_IP`; updated backend Zeroconf registration in `espet-backend/main.py` to use hostname-specific server (`espet-{host_tag}.local.`) and instance names, eliminating DNS server cache collisions. (2) fixed test suite in `test_cloud_gateway.py` corrupting `data/gateway_config.json` with dead mock ports (`http://127.0.0.1:50056/v1`) by isolating `gateway_settings.persist_path` to `tmp_dir`. (3) diagnosed upstream `agnes-2.5-flash` model endpoint timeout and switched to working fast model `qwen/qwen3.6-plus:free` / `agnes-3.0-flash`. Verified ESP32 successfully connected to local backend (`localhost:8000`), chat bindings intact, and cloud gateway polling actively (files: espet-backend/main.py, espet-backend/tests/test_cloud_gateway.py, main/sync/backend_discovery.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-06 — Restricted backend Whisper STT to English and Chinese (`voice_allowed_languages="en,zh"`): auto-detection now constrains decoding strictly to `en` and `zh`, eliminating foreign script hallucinations (such as Georgian `ka`); added confidence gate (< 3% prob for allowed languages discarded as noise) and regex script sanitizer (files: espet-backend/config.py, espet-backend/.env, espet-backend/api/voice_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-06 — Added dedicated Web STT Tester (`/stt`), remote manual mic recording controls, and VAD listener switch: implemented `tool_audio_manual_record_start` / `tool_audio_manual_record_stop` and `voice_client_set_listen` in firmware; added WebSocket frame handlers for `record_start`, `record_stop`, and `voice_listen` in `cloud_link.c`; implemented REST endpoints `POST /api/cloud/record/start`, `POST /api/cloud/record/stop`, `GET/POST /api/cloud/voice-listen`, and `GET /api/voice/last-capture-audio` with async future correlation in `cloud_router.py` and `voice_router.py`; created standalone glassmorphism tester UI at `static/stt.html` adhering to `DESIGN.MD` with live hardware recording button, transcript view, metrics grid, audio playback of the physical mic WAV, and autonomous VAD toggle to eliminate ambient wake false triggers (files: main/voice/voice_client.h, main/voice/voice_client.c, main/tools/tool_audio.h, main/tools/tool_audio.c, main/channels/cloud_link/cloud_link.c, espet-backend/services/device_registry.py, espet-backend/api/cloud_router.py, espet-backend/api/voice_router.py, espet-backend/api/ui_router.py, espet-backend/static/stt.html, espet-backend/static/cloud.html, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-06 — Denoised breadboard mic electrical buzz and boosted voice level: identified 169.17 Hz SSD1306 OLED charge-pump switching ripple + DC offset drowning out speech; added `denoise_and_boost_audio` in `voice_router.py` with DC strip, 110 Hz high-pass, 169 Hz multi-harmonic notch filter, and 12x dynamic voice normalization; updated firmware `capture_task` in `tool_audio.c` with DC-blocking high-pass filter and automatic channel railing protection (files: espet-backend/api/voice_router.py, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-06 — Fixed sound pitch change and robotic comb distortion: removed sample-by-sample Left/Right channel swapping in firmware `capture_task` (`tool_audio.c`) that was violently chopping waveforms and doubling pitch; replaced backend multi-harmonic comb filter in `voice_router.py` with linear 80 Hz studio low-cut filter and gentle 3x voice normalization, mathematically preserving authentic human voice pitch (files: main/tools/tool_audio.c, espet-backend/api/voice_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-06 — Added narrow 3-band IIR biquad notch filter (169.2 Hz, 338.3 Hz, 507.5 Hz) in `voice_router.py`: achieved 85% reduction in breadboard OLED charge-pump hum without affecting voice vowels or formants; provided hardware bottom-port acoustic aperture guidance (files: espet-backend/api/voice_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-06 — Restored natural human voice pitch and playback speed: diagnosed 2x speed / chipmunk pitch bug caused by ESP32 I2S DMA mono slot-mask decimation (`capture_task` assumed stereo interleaved buffer `[L, R]` and indexed `dma_buf[f * 2]`, dropping 50% of speech samples and halving effective sample rate to 8 kHz); implemented `detect_pitch_f0` and `check_and_fix_pitch` in `espet-backend/api/voice_router.py` to automatically detect octave-shifted pitch (> 270 Hz) and upsample 8k -> 16k, restoring exact natural human pitch (~179.8 Hz) and 1x speed; updated firmware `capture_task` in `main/tools/tool_audio.c` to read all consecutive 32-bit DMA words for genuine 16 kHz capture; updated `data/last_capture.wav` (files: espet-backend/api/voice_router.py, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-07 — Implemented BLE Wi-Fi provisioning (Apache NimBLE) with Web Bluetooth support (no app needed): resolved phone personal hotspot teardown issue during SoftAP onboarding by implementing `main/onboard/wifi_onboard_ble.c` using `wifi_prov_mgr` and `wifi_prov_scheme_ble`; device advertises `PROV_ESPET_XXXX`, scans 2.4 GHz channels, receives credentials over BLE with Security 0, verifies connection and IP, saves to NVS (`MIMI_NVS_WIFI`), and deinitializes BLE stack releasing controller RAM back to heap; added `wifi_prov` CLI command in `main/cli/serial_cli.c`; integrated standalone Web Bluetooth onboarding page in `docs/ble_prov.html` and `espet-backend/static/ble_prov.html` for single-tap pairing from phone/desktop Chrome without installing any mobile app; updated boot sequence in `main/mimi.c` and Kconfig in `sdkconfig.defaults.esp32s3` (files: main/onboard/wifi_onboard_ble.h, main/onboard/wifi_onboard_ble.c, main/mimi.c, main/cli/serial_cli.c, main/CMakeLists.txt, sdkconfig.defaults.esp32s3, docs/ble_prov.html, espet-backend/static/ble_prov.html, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-07 — Fixed crash (InstrFetchProhibited / stack overflow on Core 0) and I2S DMA reader contention when passive voice detection is disabled: diagnosed (1) `websocket_task` in `cloud_link.c` running on default 4KB stack overflowing during incoming `record_start`/`voice_listen` frames (`A1` pushed past stack boundary into heap corrupting register window return address in `console_write`); boosted `cfg.task_stack` to 8192 bytes and moved STT transcription on `record_stop` to detached worker task `stt_trans` (8KB stack); (2) eliminated `capture_task` and direct `i2s_channel_read` calls in `tool_audio_manual_record_start`, `tool_audio_rx_flush`, and `timed_capture` that collided with `mic_feeder_task` holding I2S binary semaphore (`i2s_common: i2s_channel_read(1412): The channel is not enabled`); `mic_feeder_task` is now the sole DMA reader and directly populates `s_pcm_buf` when `s_recording` is active (files: main/channels/cloud_link/cloud_link.c, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-07 — Accelerated STT inference: switched faster-whisper default model from 'small' (244M) to 'base' (74M); switched decoding to greedy (`beam_size=1`, `best_of=1`); added cross-platform auto device resolution in `voice_router.py` with Windows NVIDIA CUDA DLL bridge (`_setup_windows_cuda_dlls`) and clean CPU fallback (using Apple Accelerate on macOS); reduced STT latency from 6–10s to sub-second (files: espet-backend/config.py, espet-backend/api/voice_router.py, espet-backend/requirements.txt, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-07 — Restyled BLE provisioning page with the backend design system (DESIGN.MD tokens): replaced slate-blue palette with near-black glassmorphism (#050505 canvas, glass card, neutral gray text ramp), Outfit + JetBrains Mono fonts, dim-blue gradient primary button, neutral badge pill + brand row, and mint/red semantic log colors; JS/Web Bluetooth flow and element IDs unchanged; kept docs/ble_prov.html and espet-backend/static/ble_prov.html byte-identical and recorded the page in DESIGN.MD's implementation list (files: docs/ble_prov.html, espet-backend/static/ble_prov.html, DESIGN.MD, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-09 — Implemented Assistant Page device synchronization and redesign (Plan `docs/plans/p0911_sync_espet_backend.md`): (1) Multi-Device Registry & Store: expanded `DeviceRegistry` (`services/device_registry.py`) with persistent `data/devices.json` tracking known physical devices, online state, IPs, and last-seen timestamps; updated `config_store.py` with per-device configuration files (`data/config_<device_id>.json`) with fallback seeding, updated `_DEFAULT_USER_MD` baseline (Name: ESPET, Language: Chinese / English, Timezone: Asia/Kuala_Lumpur), and added `apply_device_report` merging incoming memory and cron jobs. (2) 2-Way Real-Time WebSocket Synchronization: added `cloud_link_send_sync_report` in firmware `cloud_link.c` reading `/spiffs/memory/MEMORY.md` and `/spiffs/cron.json` into `device_sync_report` frames sent on boot and triggered after tool writes/edits in `tool_files.c` and `tool_cron.c`; added `sync_now` frame push in `cloud_router.py` (`notify_device_sync`) waking firmware sync task via `config_sync_trigger()`; updated `config_sync.c` `fetch_json` to attach device MAC ID query parameter and `X-Device-Id` header. (3) Assistant Page Redesign: added top `<DeviceSyncBar />` card in `AssistantPage.tsx` with device selector, status badge, and immediate "Sync Now" button; redesigned `Personality.tsx` with 5 behavior presets (Default ESPET, Robotic Pet, Executive Butler, Curious Scholar, Minimalist Cyber) and dedicated `SourceField` preview/edit modal; aligned `Profile.tsx` defaults with auto-detected timezone; updated `Memory.tsx` with Title Case headers, scoped SQLite-Vec RAG search, and removed obsolete "Sync from MEMORY.md" and alert callouts; updated `Schedule.tsx` and `Skills.tsx` with Title Case headers and info tooltips, and removed full-width section callout banners (files: espet-backend/services/device_registry.py, espet-backend/config_store.py, espet-backend/api/cloud_router.py, espet-backend/api/config_router.py, main/channels/cloud_link/cloud_link.h, main/channels/cloud_link/cloud_link.c, main/sync/config_sync.h, main/sync/config_sync.c, main/tools/tool_files.c, main/tools/tool_cron.c, espet-backend/src/api.ts, espet-backend/src/store.tsx, espet-backend/src/routes.ts, espet-backend/src/views/AssistantPage.tsx, espet-backend/src/views/Personality.tsx, espet-backend/src/views/Profile.tsx, espet-backend/src/views/Memory.tsx, espet-backend/src/views/Schedule.tsx, espet-backend/src/views/Skills.tsx, espet-backend/src/components/Icon.tsx, espet-backend/src/types.ts, espet-backend/tests/test_cloud_gateway.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-09 — Implemented System Prompt, RAG Memory & Profile Alignment (Plan `docs/plans/p0911_fix_sysprompt_rag_v3.md`): (1) Refactored `Icon.tsx` using `React.forwardRef` and extended `React.SVGProps<SVGSVGElement>` with inline-flex span wrappers across `AssistantPage.tsx`, `Personality.tsx`, `Profile.tsx`, `Schedule.tsx`, `Skills.tsx`, and `Memory.tsx` to fix Ant Design tooltip hover interactions. (2) Clarified Working Memory architecture: relabeled Tier 1 in `Memory.tsx` to "Working Memory (Device Scratchpad)", replaced inline editing with bounded scrollable preview (max 220px, badges, monospace) and dedicated edit modal, updated `_DEFAULT_MEMORY_MD` in `config_store.py` to scratchpad template, and fixed `_DEFAULT_USER_MD` Name to `(not set)`. (3) Noise Filtering & Clean-up: added `NOISE_TOOLS` filter (`get_current_time`, `cron_list`, `gpio_read`, `gpio_read_all`, `read_file`, `list_dir`, `memory_search`, `memory_save`) in `_capture_episodic_action` (`cloud_router.py`), implemented `delete_noisy_actions` in `rag_service.py`, added `POST /api/memory/auto-organize` endpoint in `memory_router.py`, and added "AI Auto-Organize" button in `Memory.tsx`. (4) Profile Auto-Sync & Memory Organizer: implemented `parse_user_md`, `build_user_md`, and `patch_user_profile` in `config_store.py`; created `MemoryOrganizer` (`services/memory_organizer.py`) with markdown JSON fence stripping, smart command gating, structured LLM extraction for user profiles and semantic memories with deduplication search, and background turn evaluation in `_llm_relay`. (5) Firmware Prompt & Tool Alignment: replaced verbose 73-line prompt in `main/agent/context_builder.c` with compact neutral bootstrap retaining hardware mappings (White LED: GPIO 42, Green Button: GPIO 41, SPIFFS paths) and updated section headers; corrected `gpio_read` tool description in `main/tools/tool_registry.c` (files: espet-backend/src/components/Icon.tsx, espet-backend/src/views/AssistantPage.tsx, espet-backend/src/views/Personality.tsx, espet-backend/src/views/Profile.tsx, espet-backend/src/views/Schedule.tsx, espet-backend/src/views/Skills.tsx, espet-backend/src/views/Memory.tsx, espet-backend/config_store.py, espet-backend/services/rag_service.py, espet-backend/services/memory_organizer.py, espet-backend/api/cloud_router.py, espet-backend/api/memory_router.py, espet-backend/src/api.ts, main/agent/context_builder.c, main/tools/tool_registry.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-09 — Fixed FreeRTOS `Tmr Svc` stack overflow crash on 30-minute heartbeat: `heartbeat.c` previously registered a FreeRTOS software timer (`s_heartbeat_timer = xTimerCreate(...)`) that invoked `fopen(MIMI_HEARTBEAT_FILE, "r")` inside the `Tmr Svc` timer daemon task; because `Tmr Svc` stack is constrained to 2048 bytes (`CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=2048`), SPIFFS VFS traversal overflowed the stack after 30 minutes of uptime (`1806668 ms`) causing panic and reboot; resolved by removing the software timer and delegating periodic heartbeat polling to `cron_task_main` in `main/cron/cron_service.c` (running with dedicated 4096-byte stack) via `heartbeat_trigger()`, with zero extra heap allocations (files: main/heartbeat/heartbeat.h, main/heartbeat/heartbeat.c, main/cron/cron_service.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-09 — Implemented Hybrid Clock Synchronization and Cron Staleness Gate: resolved issue where uncalibrated 1970 clock caused accumulated one-shot cron jobs to burst simultaneously upon late time queries and remain active in web dashboard: (1) Backend Time Sync: added `server_epoch` to `GET /api/config` (`config_store.py`) and initial `time_sync` frame on WebSocket connection (`cloud_router.py`); in `apply_device_report`, automatically deactivates one-shot jobs that were deleted on device or are overdue by >15 min (`enabled = False`), updating web dashboard toggles. (2) Firmware Time Sync: `config_sync.c` and `cloud_link.c` immediately sync system clock on boot via `settimeofday()`; `wifi_manager.c` registers `esp_sntp` (`pool.ntp.org`, `time.google.com`) as secondary fallback on `IP_EVENT_STA_GOT_IP`. (3) Cron Staleness Gate: in `cron_service.c`, guarded `cron_process_due_jobs` against uncalibrated clocks (`now < 1700000000`), added 15-minute expiration drop for `CRON_KIND_AT` jobs (prevents stale message alerts), and dispatches `cloud_link_send_sync_report()` on any job state change (files: espet-backend/config_store.py, espet-backend/api/cloud_router.py, main/sync/config_sync.c, main/channels/cloud_link/cloud_link.c, main/wifi/wifi_manager.c, main/cron/cron_service.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-09 — Implemented Embodied AI Voice Pipeline with Industry VAD Constraints and Telegram Bridge (Plan `docs/plans/p0911_voice_pipeline.md`): (1) Voice Timing Constraints: updated `voice_client.c` with the Alexa/Xiaozhi 3-tier VAD model (`CMD_MAX_MS=8000`, `CMD_LEAD_MS=3500`, `CMD_SILENCE_MS=1200`, `CMD_MIN_SPEECH_MS=300`) preventing truncated conversational utterances while capping PSRAM audio buffers to 256 KB. (2) 3-Tier VAD State Machine: in `voice_run_command()`, added initial silence timeout (cancels immediately and alerts Telegram without sending empty audio to Whisper if user says nothing within 3.5s), endpointing trailing silence cutoff (ends capture cleanly after 1.2s silence following >=300ms speech), and hard cap (8.0s). (3) Interactive Telegram Feedback: added real-time Telegram status notifications for wake detection ("🎤 Wake word matched" / "🎤 I heard you! Listening..."), captured command transcription ("🗣️ You: ..."), and timeout/error alerts. (4) Response Bridge: in `mimi.c` `outbound_dispatcher_task`, routed system voice replies to bound Telegram chat ("🤖 ESPET: ...") so users receive immediate text responses while hardware speaker is unavailable, while keeping `voice_client_speak()` intact for concurrent TTS synthesis (files: main/voice/voice_client.c, main/mimi.c, docs/plans/p0911_voice_pipeline.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-13 — Guaranteed 1x normal speed playback for all voice captures: added `voice_hw_rate_8k` setting (default True in `config.py`) and updated `check_and_fix_pitch` in `voice_router.py` to deterministically upsample 8k hardware PCM to 16k regardless of voice pitch register, eliminating 2x accelerated minion sound on website playback and passing natural-tempo audio to Whisper (files: espet-backend/config.py, espet-backend/api/voice_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-13 — Implemented Multimodal Push-To-Talk (Hold-to-Speak Vision) and fixed false 'speaker active' press drop: (1) Added `tool_audio_is_speaker_busy()` in `tool_audio.{c,h}` checking only `s_playing || s_speaker_busy` and updated `button_handler.c` to use it instead of `tool_audio_is_busy()` (which was falsely dropping valid clicks whenever mic recording or passive VAD was active); updated `tool_audio_manual_record_start` to force-disarm background passive VAD so manual button record takes priority. (2) Re-architected GPIO 41 button interaction from short/long press timer into embodied Multimodal Push-to-Talk: button down immediately captures JPEG frame via `tool_camera_take_picture()` and arms manual mic recording (`tool_audio_manual_record_start`); button release stops mic capture and dispatches detached worker `snap_process_task` (8KB stack). If held (>= 400ms, speech detected), transcribes voice via Whisper STT (`voice_client_transcribe`), uploads photo to Telegram with the spoken query as caption, and pushes the question to the agent loop (`chat_id = "voice"`) for spoken TTS + Telegram AI answers; if short click (< 400ms), sends photo with default caption (files: main/tools/tool_audio.h, main/tools/tool_audio.c, main/tools/button_handler.h, main/tools/button_handler.c, main/mimi.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-13 — Decoupled firmware from mimi_secrets.h & implemented End-to-End Multimodal Vision Query with Rule-based Only-Capture branch: (1) Added `/api/camera/vision-query` endpoint in FastAPI `camera_router.py` accepting raw JPEG body and optional voice query; for empty queries (short click / only-capture), executes zero-LLM rule-based logic to send photo to Telegram with prompt caption and returns fixed prompt ("What do you want to know about this picture?") for hardware TTS speaking; for voice queries (capture & ask), uploads photo to Telegram labeled as user query ("👤 You asked: ..."), forwards image + query directly to SenseNova multimodal Vision LLM (`sensenova/sensenova-6.8-flash-lite` on `api.xkiro.com`), posts AI answer to Telegram ("🤖 ESPET: ..."), and returns the answer for device TTS playback. (2) Implemented `voice_client_vision_query` in firmware `voice_client.{c,h}` streaming binary JPEG to backend with base64 query header, completely eliminating device-side dependency on Telegram bot tokens and LLM API keys in `mimi_secrets.h`. (3) Updated `snap_process_task` in `main/mimi.c` and `telegram_send_photo_to_last_chat` in `telegram_bot.c` to route through the backend multimodal vision pipeline (files: espet-backend/api/camera_router.py, espet-backend/services/gateway_settings.py, espet-backend/data/gateway_config.json, main/voice/voice_client.h, main/voice/voice_client.c, main/mimi.c, main/channels/telegram/telegram_bot.c, main/CMakeLists.txt, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-14 — Fixed panic reset on button photo capture: (1) Diagnosed FreeRTOS assertion panic (`ESP_RST_PANIC`) caused by cross-task mutex release — `button_task` took `s_cam_mutex` in `tool_camera_take_picture()` and `snap_process_task` gave it in `tool_camera_return_picture()`, violating FreeRTOS mutex holder invariant; switched `s_cam_mutex` to a binary semaphore (`xSemaphoreCreateBinary`) which permits safe cross-task synchronization. (2) Fixed potential double-return / race condition on `s_snap_fb` by clearing `s_snap_fb = NULL` immediately upon transferring ownership to `job` in `on_camera_button_up()`. (3) Handled short noise glitches (< 80ms) in `button_handler.c` by invoking `s_up_cb(0)` to return any captured buffer instead of leaving it dangling. (4) Fixed potential null pointer dereference and memory leak in `voice_tts_fetch` (`voice_client.c`). Added detailed breadcrumb logging across snapshot lifecycle (files: main/tools/tool_camera.c, main/tools/button_handler.c, main/mimi.c, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-14 — Fixed stale/lagging frame capture on button press: resolved issue where camera returned past/old frame instead of real-time image by switching `camera_config.grab_mode` from `CAMERA_GRAB_WHEN_EMPTY` to `CAMERA_GRAB_LATEST`, and adding immediate pre-buffered stale frame flush in `tool_camera_take_picture()` to guarantee capturing a freshly exposed frame at the exact moment of the button click (files: main/tools/tool_camera.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-14 — Resolved remote tool fetch failure (`Invalid JSON from /api/tools (len=131071)`): (1) Backend tool payload optimization: in `espet-backend/api/tools_router.py`, deduplicated custom tools over MCP tools with identical names, and truncated oversized descriptions (notably `update_doc` which had 35.8 KB of docs) down to 300 characters, shrinking response payload from 134 KB to 67 KB (50% reduction). (2) Firmware buffer and schema alignment: in `main/tools/tool_registry.c`, expanded `MAX_TOOLS` from 64 to 128, increased `response_buf` allocation from 128 KB to 256 KB in PSRAM, and added fallback parsing for `"input_schema"` alongside `"parameters"`, ensuring all remote tool schemas are registered into the agent loop without buffer truncation (files: espet-backend/api/tools_router.py, main/tools/tool_registry.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-14 — Streamlined active tools and eliminated low-value/diagnostic tool bloat: (1) In `espet-backend/api/tools_router.py`, filtered out 24 low-value, administrative, or overly complex MCP tools (Gmail spam/untrash/labels/drafts, Drive permissions/base64 download/copy, Docs batchUpdate), reducing backend tools response to 21 high-impact tools and shrinking payload from 67 KB to 36 KB (~73% total reduction from initial 134 KB). (2) In `main/tools/tool_registry.{c,h}`, added `.hidden` flag to `mimi_tool_t` so low-level hardware diagnostics (`gpio_read_all`, `audio_play_tone`, `audio_record_test`, `audio_spktest`) remain callable via serial REPL (`mimi> tool_exec ...`) but are excluded from the LLM system prompt, eliminating prompt token overhead and hallucinated test-tone execution during normal conversations (files: espet-backend/api/tools_router.py, main/tools/tool_registry.h, main/tools/tool_registry.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-14 — Replaced gated Google MCP servers with native Custom REST Tools: (1) In `espet-backend/config/mcp_servers.json`, emptied external MCP servers list (`[]`), terminating connections to Developer-Preview-gated Google MCP endpoints (gmailmcp, calendarmcp, docsmcp, drivemcp). (2) In `espet-backend/api/custom_tools.py`, implemented direct standard Google REST API tools: `gmail_check_inbox` (fetches latest/unread inbox emails with sender, subject, date, snippet), `gmail_search_emails` (searches messages by query/keyword), `delete_event` (deletes calendar event by title/ID), and `read_drive_file` (exports Google Docs as plain text or downloads file content). Verified all 10 custom tools execute with Status 200 OK, slashing `/api/tools` payload from 36 KB to 4.6 KB (files: espet-backend/config/mcp_servers.json, espet-backend/api/custom_tools.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-14 — Implemented OLED Expressive Animated Eyes on SSD1306 (inspired by intellar/oled_eye_display): created procedural geometry rendering engine in `main/display/display_face.{c,h}` running as unpinned FreeRTOS task `oled_anim_task` (priority 5, 4 KB stack) rendering at ~30 FPS on 128×64 panel via I2C0 (GPIO 2 SCL, GPIO 1 SDA); features procedural rounded-rect eyes (40×40 px) with natural blinks (every 2.5–5.5s), random gaze micro-saccades (left/right/center), and emotional state morphing (Listening, Thinking, Speaking, Happy anime smiling arches, Sleeping slits); updated `display_status.c` to auto-map status updates to face states with text fallback for Wi-Fi provisioning; enhanced `oled_print` tool in `tool_registry.c` enabling LLM agent and serial CLI to set emotions directly (files: main/display/display_face.h, main/display/display_face.c, main/display/display_status.h, main/display/display_status.c, main/CMakeLists.txt, main/tools/tool_registry.c, docs/superpowers/specs/2026-11-14-oled-eye-animation-design.md, docs/superpowers/plans/2026-11-14-oled-eye-animation.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-15 — Implemented Web Desktop Chat Console with Cloud Link relay and session isolation: (1) Added `/api/cloud/ws/chat` WebSocket relay endpoint in `espet-backend/api/cloud_router.py` connecting browser directly to backend without mDNS/IP configuration hurdles; routes messages to ESP32 as `message_in` with `chat_id = "dashboard"` keeping Web conversations strictly isolated in `/spiffs/sessions/tg_dashboard.jsonl` from Telegram (`tg_<id>.jsonl`) while sharing core memory (SOUL.md, USER.md, MEMORY.md); handles `message_out` routing for `dashboard` and `ws_*`, prevents dashboard messages from leaking to Telegram Bot API, persists conversation ring buffer in `data/web_chat_history.json`, and catches `⏳ Thinking...` to stream real-time thinking states; (2) Redesigned `espet-backend/src/views/Chat.tsx` with auto-connect on mount, auto-reconnect on drop, device online status badge, persistent chat history sync, thinking spinner support, and rebuilt SPA bundle with `npm run build:app` (files: espet-backend/api/cloud_router.py, espet-backend/src/views/Chat.tsx, espet-backend/static/vendor/app/**, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-15 — Added local tools for camera snapshot, device health vitals, and clean delayed restart: implemented and registered `take_photo` (captures OV2640 frame and transmits to Telegram chat), `get_device_status` (gathers WiFi SSID/RSSI, IP address, uptime, free DRAM/PSRAM, SPIFFS flash usage, and on-chip CPU temperature via esp_driver_tsens), and `restart_device` (initiates delayed 1.5s background reboot allowing the ReAct loop to complete and deliver confirmation message to user); reinforced hardware camera capability in `context_builder.c` system prompt to prevent base LLM refusal responses (files: main/CMakeLists.txt, main/tools/tool_registry.c, main/agent/context_builder.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-15 — Added captured photo rendering to Web Chat console: updated `Chat.tsx` to render incoming photo attachments directly inside chat bubbles using Ant Design `<Image />` with native lightbox zoom/preview, updated `cloud_router.py` to auto-associate recent camera snapshots with outbound web console messages, implemented public `/api/camera/last-photo` endpoint in `camera_router.py` with automatic JPEG file persistence, and refreshed tool response in `tool_registry.c` (files: espet-backend/src/views/Chat.tsx, espet-backend/api/cloud_router.py, espet-backend/api/camera_router.py, main/tools/tool_registry.c, DESIGN.MD, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-15 — Implemented OLED Key Word Reasoning for Spoken Responses: created `espet-backend/services/oled_summarizer.py` extracting 2 punchy lines (strictly <= 16 chars each) matching SSD1306 128x64 monospace limits, delivered via `X-OLED-Line1` and `X-OLED-Line2` HTTP headers in `POST /api/voice/tts` without added latency; updated `voice_client.c` in firmware to capture headers in `rx_event_handler` and display key words via `display_status_set` while speaking the full sentence over TTS; added clean on-device word-wrapping fallback (files: espet-backend/services/oled_summarizer.py, espet-backend/api/voice_router.py, espet-backend/tests/test_oled_summarizer.py, espet-backend/scripts/test_oled_tts.py, main/voice/voice_client.c, docs/superpowers/specs/2026-11-15-oled-keyword-reasoning-design.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-15 — Enforced unconditional BLE Provisioning mode for Wi-Fi onboarding: removed SoftAP captive-portal fallback and `#if CONFIG_BT_ENABLED` branching in `wifi_onboard_ble.c`, added compile-time check `#if !CONFIG_BT_ENABLED #error ...`, retired `wifi_onboard.c` from `main/CMakeLists.txt`, updated `docs/ble_prov.html` and `espet-backend/static/ble_prov.html` labels for better Wi-Fi onboarding familiarity, and added Bluetooth NimBLE defaults to `sdkconfig.defaults`; device now strictly and always enters BLE Provisioning mode whenever Wi-Fi is disconnected (files: main/onboard/wifi_onboard_ble.c, main/CMakeLists.txt, sdkconfig.defaults, docs/ble_prov.html, espet-backend/static/ble_prov.html, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-18 — Archived Feishu channel integration, added sdkconfig Kconfig toggle, and fixed outbound dispatch fallthrough bug: (1) Diagnosed and fixed outbound dispatch bug in `main/mimi.c` where `#if MIMI_CLOUD_LINK_ENABLE ... #else` accidentally stripped the Feishu channel check and caused all cloud-routed messages (`chat_id = -5358063159`) to unconditionally fall through to `feishu_send_message()` logging `ESP_ERR_INVALID_STATE`; decoupled channel branches so cloud replies terminate independently. (2) Created `main/Kconfig.projbuild` defining `CONFIG_ESPET_ENABLE_FEISHU` (default `n`), configured `CONFIG_ESPET_ENABLE_FEISHU=n` in `sdkconfig.defaults` and `sdkconfig.defaults.esp32s3`, and wired `MIMI_FEISHU_ENABLE` macro in `main/mimi_config.h`. (3) Completely guarded out Feishu tasks, credentials, and CLI commands (`feishu_bot.c`, `feishu_bot.h`, `serial_cli.c`, `wifi_onboard.c`, `onboard_html.h`) with safe inline stubs, saving RAM, sockets, and task stacks; updated dashboard channel status in `Device.tsx` (files: main/Kconfig.projbuild, sdkconfig.defaults, sdkconfig.defaults.esp32s3, main/mimi_config.h, main/mimi.c, main/channels/feishu/feishu_bot.h, main/channels/feishu/feishu_bot.c, main/cli/serial_cli.c, main/onboard/wifi_onboard.c, main/onboard/onboard_html.h, espet-backend/src/views/Device.tsx, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-18 — Optimized Wi-Fi boot startup and added 'forget_wifi' voice/chat reset tool: (1) Fixed boot connection failures and wasted retries: eliminated `wifi_manager_scan_and_print()` from `app_main` in `main/mimi.c` which previously forced `esp_wifi_disconnect()` and channel scan 10ms after station start (causing reasons 36, 202 AUTH_FAIL, and 205 CONNECTION_FAIL); added WPA2/WPA3 PMF capability (`pmf_cfg.capable = true`, `threshold.authmode = WIFI_AUTH_WPA2_PSK`) in `wifi_manager_start()`; replaced blocking `vTaskDelay()` in `event_handler` with non-blocking `esp_timer` (`s_reconnect_timer`) preventing `sys_evt` event loop freezes; stopped Wi-Fi cleanly in `wifi_manager_trigger_ble_prov()` before rebooting. (2) Reverted button 5s BLE hold to preserve pure push-to-talk: GPIO 41 button remains strictly short click = snapshot, hold = multimodal voice query. (3) Added `forget_wifi` local agent tool in `main/tools/tool_registry.c`: enables user to reset Wi-Fi by voice or chat ("forget wifi", "reset wifi"); tool delivers confirmation, erases NVS credentials via `wifi_manager_clear_credentials()`, and triggers delayed reboot into BLE Provisioning mode (files: main/mimi.c, main/wifi/wifi_manager.c, main/tools/button_handler.h, main/tools/button_handler.c, main/tools/tool_registry.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-18 — Integrated Telegram reply for Multimodal Vision Queries & implemented LLM-powered Dual-Output OLED Architecture: (1) Diagnosed and fixed Telegram reply delivery bug in `camera_router.py` where `gw.send_message()` threw an AttributeError (`TelegramGateway` method is `send_text()`); added `send_message = send_text` alias in `TelegramGateway` (`telegram_gateway.py`) and called `send_text()` so vision query answers are reliably delivered as '🤖 ESPET: ...' to the user's Telegram chat alongside the captured photo. (2) Implemented LLM-powered Dual-Output Architecture for OLED and Telegram/Speaker: updated Vision LLM system prompt in `camera_router.py` with structured JSON schema returning 2 concise ASCII lines (<= 16 chars each) in English/Pinyin for the physical 128x64 OLED display alongside the full conversational reply; added robust markdown/JSON extraction; returned `oled_l1`/`oled_l2` in `/api/camera/vision-query`. (3) Replaced heuristic stopword regex filter with asynchronous LLM-based OLED distillation in `oled_summarizer.py` (`extract_oled_keywords_llm`) running concurrently with edge-tts audio generation in `voice_router.py` with zero added latency. (4) Updated firmware `voice_client.h`/`voice_client.c` with `voice_client_vision_query_full` and updated `snap_process_task` in `mimi.c` to immediately display the LLM's distilled lines via `voice_client_speak_with_display()` during voice playback; verified clean firmware compilation and 13/13 backend unit tests passing (files: espet-backend/services/telegram_gateway.py, espet-backend/api/camera_router.py, espet-backend/services/oled_summarizer.py, espet-backend/api/voice_router.py, espet-backend/tests/test_oled_summarizer.py, espet-backend/tests/test_camera_vision.py, main/voice/voice_client.h, main/voice/voice_client.c, main/mimi.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-19 — Added Backend Server URL configuration via BLE Provisioning (Web Bluetooth): (1) Added optional 'Backend Server URL' input field in Web Bluetooth interface (`docs/ble_prov.html` and `espet-backend/static/ble_prov.html`), automatically transmitting custom JSON payload over BLE to ESPET endpoint `custom-config` before Wi-Fi credentials. (2) Implemented custom protocomm endpoint `custom-config` in `main/onboard/wifi_onboard_ble.c` storing validated backend URL to NVS namespace `backend_cfg` (`MIMI_NVS_BACKEND`, key `url`) or clearing it if empty. (3) Updated `backend_discovery.c` to check NVS for configured backend URL, immediately priming connection base URL and providing fast fallback on mobile hotspots where client mDNS multicast is filtered. (4) Synchronized `sdkconfig` to ensure Bluetooth and NimBLE defaults match `sdkconfig.defaults`; verified clean firmware build (files: docs/ble_prov.html, espet-backend/static/ble_prov.html, main/mimi_config.h, main/onboard/wifi_onboard_ble.c, main/sync/backend_discovery.c, sdkconfig, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-19 — Prioritized runtime NVS Wi-Fi credentials over compile-time secrets: updated `wifi_manager.c` (`wifi_manager_init` and `wifi_manager_has_credentials`) to check NVS first before falling back to `MIMI_SECRET_WIFI_SSID`, preventing hardcoded build secrets from overriding runtime BLE/CLI credentials upon reboot; cleared `MIMI_SECRET_WIFI_SSID` and `MIMI_SECRET_WIFI_PASS` in `main/mimi_secrets.h` to enforce pure BLE onboarding (files: main/wifi/wifi_manager.c, main/mimi_secrets.h, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-19 — Added TLS certificate bundle verification across all backend REST HTTP clients: attached `esp_crt_bundle_attach` across `tool_registry.c` (`/api/tools`, `/api/mcp/execute`), `config_sync.c` (`/api/config`), and `voice_client.c` (`/api/voice/tts`, `/api/voice/transcribe`, `/api/camera/vision-query`), resolving `ESP_ERR_MBEDTLS_SSL_SETUP_FAILED` [0x8017] when connecting to HTTPS backend cloud endpoints (files: main/tools/tool_registry.c, main/sync/config_sync.c, main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-19 — Resolved `esp_task_stack_is_sane_cache_disabled` panic when resolving backend URL from tasks with PSRAM stack: moved NVS backend URL resolution to boot-time `backend_discovery_init()` running on DRAM main task and made `backend_discovery_base_url()` 100% in-memory; tasks with PSRAM stacks (like `camera_stream_task` and `agent_loop_task`) never invoke SPI flash / NVS, eliminating flash cache disable assertion crashes (files: main/sync/backend_discovery.h, main/sync/backend_discovery.c, main/mimi.c, main/onboard/wifi_onboard_ble.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-19 — Fixed live camera streaming write timeout and WebSocket stability over WAN/TLS: increased frame send timeout in tool_camera.c from 200ms to 1500ms (pdMS_TO_TICKS(1500)) preventing esp_transport_poll_write(0) and connection aborts when streaming 30-50KB JPEG frames over cellular hotspot / WAN; boosted camera WebSocket task_stack to 8192 bytes and buffer_size to 16384 bytes for mbedTLS TLS handling; added sent <= 0 failure handling with 100ms yield and 1500ms reconnect backoff (files: main/tools/tool_camera.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-20 — Implemented ultra-low latency single-pass OLED summary architecture and audio-first dispatch: (1) Added prompt instructions in `context_builder.c` and `spiffs_data/config/SOUL.md` for the main agent model to output `[OLED: Line 1 | Line 2]` directly in its response, dropping distillation latency to 0ms and eliminating secondary LLM API roundtrips. (2) Implemented `extract_and_strip_oled_tag` on Core 1 in `agent_loop.c` to immediately display the 2-line summary on the 128x64 OLED upon LLM completion (<5ms visual feedback) and clean the tag in-place before saving to session history and outbound dispatch. (3) Added timed text hold and extend APIs in `display_status.{c,h}` keeping summary text visible throughout audio playback plus a 5-second buffer (min 8s total) before smoothly returning to the animated living eyes face. (4) Reordered outbound dispatch in `mimi.c` for voice system messages to speak audio immediately without waiting for Telegram network latency. (5) Removed blocking secondary LLM summarizer from espet-backend `/api/voice/tts` in `voice_router.py`, speeding up TTS synthesis by 1.5–3.0s (files: main/agent/context_builder.c, spiffs_data/config/SOUL.md, main/agent/agent_loop.c, main/display/display_status.h, main/display/display_status.c, main/voice/voice_client.c, main/mimi.c, espet-backend/api/voice_router.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-20 — Decoupled action logs, added 3-factor salience rubric, conflict-resolving upsert, working memory distillation, and context-aware RAG priming: (1) Decoupled tool action traces from vector RAG: created lightweight `action_logs` table (7-day rolling TTL) in `memory_db.py` for raw execution traces, purged noisy actions from `vec_memories` via `rag_service.delete_noisy_actions`, and restricted semantic event promotion strictly to high-salience mutations (emails sent, calendar events created) in `cloud_router.py`. (2) 3-Factor Salience Rubric & Conflict Resolution: upgraded `MemoryOrganizer` prompt with durability (>30 days), actionability, and user relevance criteria, and implemented `upsert_memory_with_conflict_resolution` in `rag_service.py` to update modified facts in-place and skip duplicates. (3) Context-Aware RAG Retrieval & Attention Priming: added pronoun/entity resolution from prior turns before vector queries in `cloud_router.py`, formatted retrieved facts into a prominent `## Established User Context (Priority: Follow Strictly)` banner, and maintained sub-15ms roundtrip latency. (4) Bounded Memory Distillation & Agent Tools: added `distill_working_memory` ensuring on-device SPIFFS `MEMORY.md` stays strictly <= 3 KB, exposed `GET /api/memory/actions` and `POST /api/memory/distill` endpoints in `memory_router.py`, and added `memory_update` and `memory_forget` MCP tools to `custom_tools.py` with full unit test coverage (files: espet-backend/services/memory_db.py, espet-backend/services/rag_service.py, espet-backend/services/memory_organizer.py, espet-backend/api/cloud_router.py, espet-backend/api/memory_router.py, espet-backend/api/custom_tools.py, espet-backend/tests/test_memory_system.py, espet-backend/tests/test_rag_service.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-20 — Fixed mic + camera concurrency lockout and implemented voice interruption / barge-in: decoupled VAD wake listener from agent thinking state (`s_busy`) in `voice_client.c` so listening remains active during tool execution and camera snapshots; added voice interruption APIs to abort in-flight LLM/tool turns and skip obsolete TTS; fixed manual recording state leak in `mimi.c` on glitch pulses in `on_camera_button_up(0)`; resolved INMP441 L/R auto-switch latch in `tool_audio.c`; added 35s thinking watchdog and 32s recording watchdog (files: main/agent/agent_loop.c, main/mimi.c, main/tools/tool_audio.c, main/voice/voice_client.c, main/voice/voice_client.h, docs/plans/p2011_fix_mic_camera_tradeoff_plan.md, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-20 — Implemented Voice Self-Modification Suite and Dynamic VAD Listening: (1) Dynamic VAD & Flexible Listening: extended utterance capture cap from 2.5s to 15s (`VOICE_MAX_RECORD_MS = 15000`) with unified 1200ms trailing silence cutoff (`CMD_SILENCE_MS = 1200`), allowing full conversational prompts to be captured without truncation in both one-shot and two-step wake modes. (2) Self-Modification Backend Tools: added `update_personality` (presets: default, robotic_pet, butler, scholar, or custom traits/rules), `update_user_profile` (name, language, timezone, preferences), `manage_memory` (unified vector RAG + on-device MEMORY.md working notes), `manage_skills` (CRUD + toggle for modular markdown skills), `manage_schedules` (cron jobs), and `manage_device_settings` (name/timezone) in `custom_tools.py`, directly updating `config_store.py` and pushing real-time WebSocket sync to the device. (3) Instant SPIFFS Synchronization: updated `tool_registry.c` to automatically trigger `config_sync_trigger()` upon execution of any self-modification tool, immediately reflecting changes in `/spiffs/config/` and `/spiffs/skills/` without waiting for the 5-minute sync timer. (4) System Prompt Guidance: updated `context_builder.c` instructing the ReAct agent on its autonomous self-modification authority and tools (files: main/voice/voice_client.c, espet-backend/api/custom_tools.py, main/tools/tool_registry.c, main/agent/context_builder.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-20 — Fixed audio recording latch on command timeout: added missing `tool_audio_capture_disarm()` in `voice_client.c` when wait-for-speech times out (3.5s) so `s_recording` is reset, allowing the timeout beep to play and preventing VAD listening from pausing indefinitely; added abort checks in `voice_run_command()`; updated feeder debug log labels (files: main/voice/voice_client.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-20 — Real-Time Frontend Web UI, Backend & ESP32 Synchronization: (1) Hardware Identity in Remote Tools: updated `tool_remote_execute` in `main/tools/tool_registry.c` to read Wi-Fi STA MAC address (`ESP-%02X%02X...`) and send `X-Device-Id` HTTP header to `/api/mcp/execute`, guaranteeing remote tools target the active hardware device. (2) Backend Device Targeting & Web Broadcast: updated `espet-backend/api/mcp_router.py` to accept `X-Device-Id` and inject `device_id` into tool arguments; updated `espet-backend/services/device_registry.py` with absolute path resolution and online/MAC-derived device prioritization; updated `notify_device_sync` in `espet-backend/api/cloud_router.py` to broadcast `config_updated` frames to active browser WebSocket clients. (3) Byte-for-Byte Personality Alignment: aligned all 5 presets (`default`, `robotic_pet`, `butler`, `scholar`, `minimalist`) and alias dictionary in `espet-backend/api/custom_tools.py` with `Personality.tsx`, ensuring canonical matching activates the matching preset tag. (4) Reactive Web Dashboard: added lightweight `/api/config/version` polling (2.5s interval and on window focus) in `espet-backend/src/store.tsx`; updated `Personality.tsx` with strict `checked={isCurrentActive}` radio logic and reactive draft synchronization; updated `Profile.tsx`, `Memory.tsx`, and `Skills.tsx` with reactive `useEffect` hooks reloading drafts and data on version bumps; compiled SPA distribution via `npm run build:app` (files: main/tools/tool_registry.c, espet-backend/services/device_registry.py, espet-backend/api/mcp_router.py, espet-backend/api/cloud_router.py, espet-backend/api/custom_tools.py, espet-backend/src/api.ts, espet-backend/src/store.tsx, espet-backend/src/views/Personality.tsx, espet-backend/src/views/Profile.tsx, espet-backend/src/views/Memory.tsx, espet-backend/src/views/Skills.tsx, espet-backend/src/views/Device.tsx, espet-backend/static/vendor/app/**, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-27 — Removed voice interruption / barge-in mechanism and restored strict half-duplex turn-taking: removed `s_abort_requested` and `voice_client_{request_abort,is_abort_requested,clear_abort}` from `voice_client.{h,c}`, removed all abort checks from `voice_run_command()`, `voice_speak_internal()`, `agent_loop.c`, and `tool_audio_play_wav()` in `tool_audio.c`, restored `s_busy` gating in `voice_task()` so wake-word listening pauses cleanly during active turns while preserving hardware fixes (button-glitch disarm, INMP441 L/R restore, 32s/35s watchdogs, timeout disarm) (files: main/voice/voice_client.h, main/voice/voice_client.c, main/agent/agent_loop.c, main/tools/tool_audio.c, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-27 — Upgraded `/stt` into a full Voice & Speaker Hardware Debug and Regression Testing Bench: (1) Backend Debug & Speaker Control APIs: added `GET /api/voice/debug/config`, `GET /api/voice/debug/voices`, `POST /api/voice/debug/transcribe` (supports `use_last_capture`, WAV/PCM upload, `skip_pitch_fix`, `skip_denoise`, `skip_rms_gate`, `skip_confidence_gate`, and acoustic/logprob telemetry without mutating `last_capture.wav`), `POST /api/voice/debug/tts` (returns 16 kHz WAV with timing/format/OLED headers), and `POST /api/voice/debug/regression` (non-destructive suite covering wake-word parsing, PCM/WAV roundtrip, 8k→16k resampling, notch denoise, silence gate, synthetic Whisper check, TTS WAV contract, and TTS→STT loopback) in `espet-backend/api/voice_router.py`; fixed `vad_filter=False` retry segment assignment; added `tts_say_status` handling and `POST /api/cloud/tts-say` in `espet-backend/api/cloud_router.py` to trigger physical ESP32 MAX98357A speaker + OLED tests cleanly over `/ws/device`. (2) Debug Bench UI (`espet-backend/static/stt.html`): rebuilt `/stt` as a dense functional bench with device/API-key/VAD controls, live runtime config readout, ESP32 INMP441 capture + re-run STT, browser mic (16 kHz WAV) & file upload STT isolation, waveform canvases, browser TTS + ESP32 speaker playback + TTS→STT loopback, one-click regression runner, production contract verifier, and event log (files: espet-backend/api/voice_router.py, espet-backend/api/cloud_router.py, espet-backend/static/stt.html, espet-backend/tests/test_voice_debug_bench.py, .agent/SYSTEM_OVERVIEW.md).
- 2026-11-27 — Fixed speaker output failures, playback concurrency races, and destructive GPIO bus policy: (1) Generation-Scoped Wake-Word Barge-In: decoupled `s_vad_busy` from `s_agent_turn_active` in `voice_client.{h,c}` and added monotonic `uint32_t turn_gen` to `mimi_msg_t` (`message_bus.h`), propagated across `agent_loop.c` → `outbound_dispatch_task` (`mimi.c`) → `tool_audio_play_wav_turn()`, eliminating self-abort on `'Yes?'`, standalone `tts_say` / `snap_process_task` / `audio_spktest`, and newly woken commands while supporting clean barge-in without a 35s deaf window. (2) Speaker Session Refcounting & TX Serialization (`tool_audio.{h,c}`): derived `s_speaker_busy` under `portMUX_TYPE s_audio_mux` from refcounted `s_speaker_session_depth > 0 || s_playing || s_cache_clearing`, replaced `tool_audio_clear_cache()`'s unsafe `was_busy` restore, added non-recursive `s_tx_mutex` strictly around I2S TX writes, drained 160 ms of zero frames through TX DMA before unparking `mic_feeder_task`, wrapped `audio_spktest_execute()` in a continuous speaker session, and fixed WAV `fmt ` bounds check (`idx + 24 <= len`), unsigned sample shift, and `play_tone` volume check. (3) GPIO Policy & Pinout Docs: restricted `MIMI_GPIO_ALLOWED_CSV` (`gpio_policy.h`) to `"41,42"` protecting active I2S (`21,38,39,40`), OLED I2C (`1,2`), and Camera DVP (`4-18`) pins, removed legacy GPIO 38/39 pull-ups in `tool_gpio.c`, updated `tool_registry.c` `gpio_read_all` description, and aligned `AGENTS.md` and `.agent/ESPET_PINS.MD` (files: main/bus/message_bus.h, main/voice/voice_client.h, main/voice/voice_client.c, main/tools/tool_audio.h, main/tools/tool_audio.c, main/agent/agent_loop.c, main/mimi.c, main/tools/gpio_policy.h, main/tools/tool_gpio.c, main/tools/tool_registry.c, AGENTS.md, .agent/ESPET_PINS.MD, .agent/SYSTEM_OVERVIEW.md).
