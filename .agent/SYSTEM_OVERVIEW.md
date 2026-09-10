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
> | **Dashboard** | React 19 + antd 6 + @lobehub/ui SPA (`espet-backend/src/`) |
> | **LLM** | OpenAI-compatible endpoint (LM Studio / cloud) or Anthropic API |
> | **Docs** | This file · `.agent/FR.MD` (locked functional reqs) · `docs/ARCHITECTURE.md` (firmware detail) · `.agent/ESPET_PINS.MD` (wiring) |

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
| `/spiffs/skills/*.md` | web dashboard (config sync) **and** the agent (`write_file`) | context builder | Reusable skill playbooks |

> **Locked Memory & RAG Requirements (`.agent/FR.MD`)**:
> Local SPIFFS holds fast, active working memory. Aged daily notes (`YYYY-MM-DD.md`)
> automatically archive to the backend (`POST /api/memory/archive`), where they are chunked
> and embedded into SQLite (`db/espet.db`) via `sqlite-vec` and local CPU `fastembed`.
> During ReAct turns, the agent queries persistent memory via remote tool calls
> (`memory_search` / `memory_save`) with strict `user_id` query isolation.
> See authoritative spec: [`.agent/FR.MD`](./FR.MD).

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
| **Single tool endpoint** | `POST /api/mcp/execute` forwards `{tool_name, arguments}` to whichever MCP server or internal tool owns that tool — adding a new tool needs **zero code changes** on firmware |
| **Persistent Memory & RAG (Locked: FR.MD)** | Manages local vector RAG via `sqlite-vec` + local CPU `fastembed` in unified SQLite (`db/espet.db`). Endpoints for daily note archiving (`POST /api/memory/archive`), conversational search (`memory_search`), and fact saving (`memory_save`) with strict `user_id` isolation. Governed by [`.agent/FR.MD`](./FR.MD). |
| **Device-User Identity Binding** | Maps physical ESP32 devices to owners (`devices.device_id -> user_id`), ensuring multi-user isolation with zero cross-contamination without needing heavy JWT auth on firmware. |
| **Auth manager** | One-time Google OAuth consent at `/auth/google` (launched from the dashboard's **Login with Google** popup); access + refresh tokens in `db/tokens.db` (migrating to unified `espet.db`), refreshed automatically. After a re-auth the dashboard calls `POST /api/mcp/reconnect` to re-discover tools with the fresh token — no backend restart needed. The ESP32 only ever presents the shared device API key |
| **Config source of truth** | `config_store.py` + `data/config.json` (migrating to SQLite `db/espet.db` per FR.MD) — everything the web dashboard edits (see §5) |
| **Dashboard host** | Serves the control center UI at `/` (`api/ui_router.py` + `static/`) |

### 4.2 API surface

| Method | Endpoint | Auth | Purpose |
|---|---|---|---|
| `GET` | `/` | — | Web dashboard |
| `GET` | `/health` | — | Status + connected MCP servers/tool counts |
| `GET` | `/auth/google` · `/auth/status` | — | OAuth login + link state (status also reports `token_valid` + `token_updated_at`) |
| `GET` | `/api/tools` | device key | Full tool schema list (feeds the agent's system prompt) |
| `POST` | `/api/mcp/execute` | device key | Execute any remote tool (MCP or internal tools like `memory_search` / `memory_save`) |
| `POST` | `/api/mcp/reconnect` | device key | Re-run MCP discovery with the current token (after Google re-auth) |
| `POST` | `/api/memory/archive` | device key | Upload aged daily notes (`YYYY-MM-DD.md`) from SPIFFS to SQLite RAG (FR.MD) |
| `GET` | `/api/config` | device key | **Config bundle the device pulls** |
| `GET` | `/api/config/version` | device key | Per-section versions (cheap poll) |
| `PUT` | `/api/ui/profile` · `/soul` · `/memory` | device key | Edit USER.md / SOUL.md / MEMORY.md |
| `GET/POST/DELETE` | `/api/ui/memory[/{id}]` | device key | Dashboard memory search, manual creation, and deletion (FR.MD) |
| `GET/POST` | `/api/ui/devices[/bind]` | device key | List devices and re-bind physical ESP32 hardware to user owners (FR.MD) |
| `PUT` | `/api/ui/device` | device key | Name, timezone (IANA + generated POSIX TZ), sync interval |
| `GET/POST/PUT/DELETE` | `/api/ui/jobs[/{id}]` | device key | Web-managed cron jobs |
| `GET/POST/PUT/DELETE` | `/api/ui/skills[/{id}]` | device key | Web-managed skill markdown files |
| `GET` | `/api/skills/sync` | device key | Skill bundle the device pulls when `versions.skills` advances (enabled skills only) |
| `GET/PUT` | `/api/ui/avatar` | device key | Agent avatar dress-up state (dashboard-only) |

Auth model: Physical ESP32 hardware binds to an owner `user_id` in SQLite. Requests from
devices present `Bearer <device_api_key>` which resolves to their bound owner (default `'default_user'`).
Queries strictly scope with `WHERE user_id = :uid` and vector join on `memory_chunks.user_id`
to guarantee zero cross-tenant leakage (see [`.agent/FR.MD`](./FR.MD)). CORS open on LAN;
put TLS in front before leaving the local network.

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
| **Home** (Overview) | New-owner landing: onboarding checklist (backend / Google / personalised / device address), stat cards, **charts** (tools-per-server donut, client-sampled health sparkline — the server sync-activity chart awaits the metrics endpoint), device card, Google OAuth popup + MCP reconnect |
| **Agent** | 3D toy robot (unchanged `ToyAgent`) in a `<ToyAvatar>` React wrapper; auto-style derives paint/hat/glasses/antenna/bowtie/mood from SOUL.md + memory + jobs + name; dress-up workbench modal, "Surprise me", reset-to-auto → `/api/ui/avatar` (debounced) |
| **Chat** | **Direct browser → ESP32 WebSocket gateway** (`ws://<device-ip>:18789`, `ws_server.c` protocol) — transparent prompt/response log, typing indicator (60s timeout), offline state; transcript in localStorage. Device IP set in Device view. (Backend relay + heartbeat = next plan) |
| **Camera** | Live view of the OV2640: polls `http://<device>:18789/camera.jpg` on an interval (2 fps … 1/5 s, shared device address with Chat), Snapshot one-shot, LIVE tag + last-frame time; graceful offline state until firmware adds the route (NEXT_PLAN §6). LAN-direct, backend not involved |
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
│   ├── db/tokens.db           OAuth token store (SQLite, consolidating into espet.db)
│   ├── db/espet.db            Unified SQLite DB: config, RAG memory (sqlite-vec), devices (FR.MD)
│   ├── package.json           front-end vendor deps (animejs, three) + esbuild
│   ├── tools/toy-entry.js     esbuild entry → static/vendor/toy.bundle.js (3D agent)
│   ├── static/                index.html · style.css · app.js
│   │                          toy.bundle.js (three.js) · models/robot.glb
│   └── data/config.json       live config (gitignored, seeded on first run)
├── DESIGN.MD                  dashboard design system doc (tokens, components, layout)
├── docs/ARCHITECTURE.md       firmware-deep architecture
├── .agent/FR.MD               locked functional requirements (Memory, RAG, SQLite, Multi-user)
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

---

## Recent Changes

Append-only session log. Newest first. One line per change:
`- YYYY-MM-DD — summary (files: ...)`.

- 2026-09-10 — **Locked Functional Requirements for Memory, RAG & Storage (.agent/FR.MD):** formalized and locked architecture decisions: 100% embedded/local-first SQLite backend (db/espet.db) with sqlite-vec (384d) and local CPU fastembed (zero cloud DB, zero external embedding API keys), single shared DB with strict user_id foreign keys and query joins (zero cross-user leakage), physical device-to-user binding table, and dual-pipeline memory ingestion (aged daily notes auto-archive + agent memory_save/memory_search ReAct tools + React dashboard memory management). Updated SYSTEM_OVERVIEW.md to map FR.MD as authoritative locked functional requirements (files: .agent/FR.MD, .agent/SYSTEM_OVERVIEW.md).

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
