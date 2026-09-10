# ESPET — Backend / Firmware Roadmap (NOT implemented this session)

This session was **frontend-only**: the dashboard was rebuilt as a React 19 +
antd 6 + `@lobehub/ui` SPA that consumes the *existing* FastAPI endpoints
unchanged. Everything below was deliberately deferred. Each item is scoped so it
can be picked up independently.

---

## 1. GPIO pin remap (firmware + data) — HIGHEST PRIORITY

The hardware was rewired. Authoritative map now in `.agent/ESPET_PINS.MD`.
The firmware and all seed text still carry the **old** numbers, so the device is
currently mis-wired relative to the docs. The new map:

| Peripheral | Old GPIO | **New GPIO** |
|---|---|---|
| OLED SCL | 21 | **2** |
| OLED SDA | 47 | **1** |
| Green button (PTT) | 39 | **41** |
| White status LED | 40 | **42** |
| I2S BCLK/SCK | 41 | **38** |
| I2S WS/LRC | 42 | **39** |
| Mic SD (INMP441) | 1 | **40** |
| Speaker DIN (MAX98357A) | 2 | **21** |
| White button | 38 | **REMOVED** (38 is now I2S BCLK) |

Touch points (verified by audit):
- `main/mimi_config.h` — the `MIMI_GPIO_*` #defines (source of truth).
- `main/tools/tool_oled.c:16-17`, `main/tools/button_handler.h:7-8`,
  `main/tools/tool_gpio.c:121` — replace duplicated literals with the macros.
- `main/tools/button_handler.c`, `main/mimi.c` — log strings + the white-button
  ISR/edge config. **Removing the white button also removes its camera-snapshot
  trigger** — decide the new camera trigger (agent tool call, or reassign to a
  button) before deleting.
- `main/agent/context_builder.c:47-60` and `main/tools/tool_registry.c` — the
  LLM prompt/description pin text.
- `main/tools/gpio_policy.h` allowlist → `1,2,21,38,39,40,41,42` (drop 47; 38
  is now a clock, not a button; keep camera bus 4–18 blocked).
- Seed markdown that hardcodes pins: `spiffs_data/skills/gpio-control.md`,
  `spiffs_data/config/SOUL.md`, and the **duplicate** default skill embedded in
  `espet-backend/config_store.py` (lines ~116-158). Both skill copies must be
  kept in sync.
- Docs: `docs/telegram_hardware_testing.md`, `AGENTS.md` pinout table.
- Verify: `idf.py fullclean && idf.py build` (CI container).

## 2. SQLite storage (user decision: store user data in SQLite)

Replace the JSON-file store with SQLite via `aiosqlite` (already a dependency,
already used by `auth/token_store.py`). The React frontend is **storage-agnostic**
— API shapes stay identical, so this is invisible to the UI and to the ESP32.

- New `store/db.py`: single aiosqlite connection, WAL mode, schema init,
  `asyncio.Lock` for read-modify-write (mirrors token_store pattern).
- Tables: `documents(name PK, content, version, updated_at)`,
  `jobs(id PK, …)`, `skills(id PK, …)`, `settings(key PK, value_json)` (device,
  avatar, version counters), `sync_events(id, ts, versions_json)`,
  `chat_messages(id, chat_id, role, content, ts)`, `device_state(key, value_json)`.
- Rewrite `config_store.py` internals to SQLite, keep its public function names;
  routers `await` them (satisfies the async-only rule).
- One-time migration on startup: import legacy `data/config.json` → SQLite,
  rename to `config.json.migrated`, **preserve version counters** so devices
  don't re-pull.

## 3. Metrics endpoint (unlocks the Home sync-activity chart)

The Home health sparkline is currently **client-sampled** (localStorage) because
there is no server history. Add:
- On `GET /api/config` (device pull) append `{ts, versions}` to `sync_events`
  (ring, ~500 rows).
- `GET /api/ui/metrics` (device-key guarded) → `{sync_events, counts}`.
- Frontend: replace the localStorage sparkline with a real sync-activity
  AreaChart + "device last synced / online" indicator.

## 4. Chat relay + device heartbeat (durable, multi-device)

This session's Chat view talks **browser → ESP32 gateway directly** (port 18789,
`ws_server.c`) with the transcript in localStorage. That works today but is
per-browser and needs the device IP. The durable version:
- Firmware: after each successful config pull, `config_sync.c` POSTs
  `/api/device/heartbeat {ip, fw}` (device already knows its IP via esp_netif) —
  ~30 lines, reuses the existing HTTP client + Bearer auth. Gives the backend
  the device address + online state with zero user config.
- Backend `chat/` module: async WS client to `ws://<device_ip>:18789/` speaking
  the existing protocol; browser relay `WS /api/chat/ws`; persist transcript to
  `chat_messages`. uvicorn[standard] already provides websockets.
- Frontend: point Chat at the backend relay instead of the raw device socket;
  history survives reloads and multiple browsers.

## 6. Camera snapshot endpoint on the device (unlocks the Camera live view)

The dashboard's **Camera** page polls `http://<device>:18789/camera.jpg` and
repaints on an interval (2 fps … 1 / 5 s). The firmware does not serve that
route yet — `ws_server.c` registers only the `/` WebSocket handler.

Touch points:
- `main/gateway/ws_server.c`: register a second `httpd_uri_t` with
  `.uri = "/camera.jpg"`, `.method = HTTPD_GET`, `.is_websocket = false`.
  Handler: `tool_camera_take_picture()` → send the `camera_fb_t` buffer with
  `httpd_resp_set_type("image/jpeg")` + `httpd_resp_send(fb->len)` →
  `tool_camera_return_picture(fb)`. Use `Connection: close` so browsers don't
  stall on keep-alive.
- Concurrency: camera capture is exclusive — guard with a mutex so a snapshot
  request never races other capture paths; reply 503 while busy.
- CORS: add `Access-Control-Allow-Origin: *` (LAN-only device; the dashboard
  is served from a different origin/port).
- Cost: one JPEG (~20–150 KB at SVGA) per request — keep the poll rate at
  ≤ 2 fps so the gateway task never starves the agent loop.
- Verify: `idf.py build`, then from the LAN
  `curl -o f.jpg http://<ip>:18789/camera.jpg` and open the Camera view — the
  frame should refresh at the selected rate.

## 5. Minor frontend follow-ups (optional)

- Move the hardcoded `API_KEY` out of the bundle (serve it from `/` or a login)
  before any non-LAN exposure.
- lobe-charts peer `@lobehub/ui ^4.3.3` vs installed 5.x — works at runtime;
  revisit when charts ships a v5 peer.
- Consider lazy-loading the shiki grammar sidecars only for languages actually
  used in skills (bundle hygiene; 415 chunks today, all lazy).
