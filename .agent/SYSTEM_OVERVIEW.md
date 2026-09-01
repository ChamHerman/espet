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
        │  voice (mic) · buttons · Telegram / Feishu · web dashboard
        ▼
┌────────────────────────────────────────────────────────────────────┐
│                     ESP32-S3 DEVICE  (MimiClaw)                    │
│                                                                    │
│  ┌──────────── INPUT ────────────┐   ┌──────── OUTPUT ─────────┐  │
│  │ INMP441 I²S microphone        │   │ MAX98357A I²S speaker    │  │
│  │ Green button (PTT / trigger)  │   │ SSD1306 OLED (I²C)       │  │
│  │ White button (camera / PT)    │   │ Telegram / Feishu reply  │  │
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
│  │ LOCAL  : gpio, oled, audio,│               │                    │
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
python main.py                    # starts on http://0.0.0.0:8000
```

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

### Rebuild vendor assets (optional — only if you change the 3D agent or motion)

```bash
cd espet-backend
npm install
npm run build:vendor
```

Committed vendor files in `static/vendor/` already work — this step is only
needed if you make changes to `tools/toy-entry.js` (the 3D robot viewer) or
update `animejs`/`three` in `package.json`.

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

Shared board: **ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM), partitioned by `partitions.csv`
with a SPIFFS partition for agent files.

| Peripheral | Chip / Part | Pins | Notes |
|---|---|---|---|
| OLED display | SSD1306 | SCL 21, SDA 47 | I²C, tool results + proactive cards |
| Microphone | INMP441 (I²S) | SD 1, SCK 41, WS 42 | L/R tied to GND |
| Speaker amp | MAX98357A (I²S) | DIN 2, BCLK 41, LRC 42 | Shares I²S bus with the mic |
| Green button | Push (input pull-up) | 39 | Push-to-talk / trigger |
| White button | Push (input pull-up) | 38 | Short = camera snapshot, long = video |
| White LED | — | 40 | Status / feedback |
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
wifi start ──fail──▶ captive portal onboarding (blocks, reboots on success)
│ok
outbound dispatch task → OLED boot animation → agent_loop_start
→ telegram/feishu start → cron_start → heartbeat_start → ws_server → config_sync_start
```

### 3.2 The agent loop (`agent/agent_loop.c`) — ReAct pattern

1. **Context build** (`context_builder.c`): system prompt = `SOUL.md` (personality) +
   `USER.md` (profile) + `MEMORY.md` (long-term memory) + recent daily notes + loaded
   skill files + the tool list (local registry + remote schemas from `GET /api/tools`)
   + recent conversation history (`session_mgr.c`).
2. **LLM call** (`llm_proxy.c`): streams from the configured provider (LM Studio /
   OpenAI-compatible via `HOST_IP:1234`, or Anthropic). Optionally emits a working
   status to the caller's channel.
3. **Dispatch**: the LLM answers with either `text` (→ outbound queue → speaker/OLED/
   chat) or `tool_use`. Each tool_use goes to the **tool registry**:
   - tool exists locally → executed on-chip (`tool_gpio`, `tool_oled`, `tool_audio`,
     `tool_camera`, `tool_files`, `tool_cron`, `tool_get_time`, `tool_web_search`)
   - otherwise → `POST /api/mcp/execute` to the backend (`tool_registry.c`)
4. **Loop**: the tool result is fed back as a new LLM message. Max 10 iterations /
   4 tool calls per request. Safety: GPIO writes go through `gpio_policy.c`.

### 3.3 Memory system (`memory/`)

| File (SPIFFS) | Written by | Read by | Purpose |
|---|---|---|---|
| `/spiffs/config/SOUL.md` | web dashboard (config sync) | context builder | Personality, values, hard behavior rules |
| `/spiffs/config/USER.md` | web dashboard (config sync) | context builder | Who the user is, language, timezone, context |
| `/spiffs/memory/MEMORY.md` | web dashboard **and** the agent (consolidation) | context builder | Long-term facts |
| `/spiffs/memory/YYYY-MM-DD.md` | the agent | context builder (last N days) | Daily episodic notes |
| `/spiffs/sessions/*` | session_mgr | agent loop | Rolling conversation history |
| `/spiffs/skills/*.md` | flashed / skill_loader | context builder | Reusable skill playbooks |

### 3.4 Proactive daemons

- **`cron/cron_service.c`** — up to 16 jobs in `/spiffs/cron.json`. Two kinds:
  `every` (interval seconds) and `at` (one-shot epoch, optionally
  `delete_after_run`). When a job fires it pushes a normal message onto the
  inbound bus with a destination `channel` (`system` = OLED + voice, or
  `telegram` + `chat_id`), so the agent treats it exactly like a user message
  — and can reply wherever the job says.
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
- **`sync/config_sync.c`** — web-config pull (see §5).

### 3.5 Channels (`channels/`)

- **Telegram** (`telegram_bot.c`) — long-polling inbound, outbound text + photos
  (`telegram_bot_photo.c`). Bot token stored in NVS (`telegram_set_token`),
  set once via serial CLI. Replies go to the originating chat.
- **Feishu / Lark** (`feishu_bot.c`) — app credentials in NVS, webhook receiver
  on :18790.
- **Serial CLI** (`cli/serial_cli.c`) — local console for setup/debug, works
  without WiFi.
- **WebSocket gateway** (`gateway/ws_server.c`, :18789) — LAN clients for
  live inspection.

### 3.6 Connectivity & onboarding

`wifi/wifi_manager.c` (credential store + retry/backoff) and
`onboard/wifi_onboard.c` (captive portal: `/scan`, `/config`, `/save` —
first-boot setup from a phone). `ota/` for firmware updates.

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
| `GET/PUT` | `/api/ui/avatar` | device key | Agent avatar dress-up state (dashboard-only) |

Auth model: one shared **device API key** (`Bearer`, set in `.env` and
`main/mimi_config.h`) for everything — single-user scope by design. CORS open
on LAN; put TLS in front before leaving the local network.

### 4.3 What the device-pull bundle looks like

`GET /api/config` (≈1–5 KB, fits the 32 KB device buffer with headroom):

```json
{
  "versions": { "user": 2, "soul": 1, "memory": 1, "device": 2, "jobs": 3 },
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
```

Rules implemented in `main/sync/config_sync.c`:

1. **Per-section versions** (`user`, `soul`, `memory`, `device`, `jobs`) — a
   section is applied only when its version is strictly greater than the last
   applied value, persisted in `/spiffs/config/sync_state.json`.
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
5. **Memory semantics**: a dashboard edit wins *once* (version bump), after
   which the agent freely consolidates memory again — the UI shows this warning.
6. **Avatar is dashboard-only**: the `avatar` config section (3D agent look)
   is version-tracked in the store but **excluded from the device bundle** —
   the ESP32 never pulls it.

---

## 6. Web Dashboard (`espet-backend/static/`)

Single page, no build step. Vanilla JS (`app.js`), hand-rolled design system
(`style.css`: dark glassmorphism, slate + emerald, Outfit + JetBrains Mono),
`anime.js` for motion, **three.js + the RobotExpressive glTF** (CC0 toy robot)
for the agent showcase. Front-end vendor libraries are installed via npm
(`espet-backend/package.json`) and pre-bundled with esbuild into
`static/vendor/` — the page itself stays build-free at runtime.

| View | What it does |
|---|---|
| **Overview** | Live health, Google link state with **Login with Google** button (OAuth runs in a popup, then MCP servers auto-reconnect), MCP servers, job counts, config versions |
| **Agent** | Big centered 3D showcase of a toy robot. The robot dresses itself from the agent's own settings — SOUL.md keywords, memory count, scheduled jobs and the user name map to paint job, hat, glasses, antenna, bow tie and mood animation (wave/dance/idle). Drag to spin. Dress-up workbench opens in a modal: hats (top hat/party cone/propeller/beanie), round specs or visor, antenna, bow tie, body/head/hat colors, mood. "Surprise me" dice, reset-to-auto. Saved to the backend (`avatar` config section) but intentionally not synced to the device |
| **Profile** | USER.md editor (structured fields + free-text context) |
| **Personality** | SOUL.md editor + reset to default |
| **Memory** | MEMORY.md editor, quick-note append, clear-with-confirm |
| **Schedule** | Cron job CRUD (recurring / one-time, device or Telegram delivery) |
| **Device** | Name, timezone, sync interval, channel overview, sync explainer |
| **Tools** | Searchable/filterable explorer of all discovered MCP tools + JSON schemas |

UX details: dirty-state tracking with unsaved-changes guard, toast feedback,
confirm dialogs for destructive actions, keyboard focus states, responsive
(sidebar collapses under 820px), `prefers-reduced-motion` respected
(animations are progressive enhancement — the page works with JS/anime absent).

---

## 7. End-to-end flows

### 7.1 Reactive (Jeff): *"Email Dr Wong a summary of my proposal doc"*

```
voice → INMP441 → agent_loop
  ctx = SOUL + USER + MEMORY + transcript + tools
  LLM #1 → tool_use(docs_read_document)  → POST /api/mcp/execute → Google Docs MCP
  LLM #2 → tool_use(gmail_send_email)    → POST /api/mcp/execute → Gmail MCP
  LLM #3 → end_turn("Done — summary sent.")
speaker + OLED reply
```

### 7.2 Proactive (Herman): scheduled briefing

```
cron.json job due → cron_service pushes "Give me a summary of my day"
→ agent loop (identical to a user message)
→ tools (calendar_list_events, gmail_search_threads…) → reply to channel
  (system = OLED + voice, or telegram chat_id)
```

### 7.3 Configuration: dashboard → device

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
├── spiffs_data/               default SOUL.md · USER.md · MEMORY.md · skills
├── espet-backend/
│   ├── main.py                FastAPI app wiring
│   ├── config_store.py        data/config.json store + versioning + TZ/jobs helpers
│   ├── api/                   health · mcp_router · tools_router · auth_router
│   │                          config_router (device pull + UI CRUD) · ui_router (dashboard)
│   ├── auth/                  device_auth (Bearer key) · oauth_manager · token_store
│   ├── mcp/                   client + auto-discovery registry
│   ├── db/tokens.db           OAuth token store (SQLite)
│   ├── package.json           front-end vendor deps (animejs, three) + esbuild
│   ├── tools/toy-entry.js     esbuild entry → static/vendor/toy.bundle.js (3D agent)
│   ├── static/                index.html · style.css · app.js
│   │                          vendor/: anime.iife.min.js (anime.js UMD) ·
│   │                          toy.bundle.js (three.js) · models/robot.glb
│   └── data/config.json       live config (gitignored, seeded on first run)
├── docs/ARCHITECTURE.md       firmware-deep architecture
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
