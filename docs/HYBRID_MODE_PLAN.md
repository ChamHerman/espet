# ESPET Hybrid Mode — Implementation Plan (Plan C-Hybrid, FYP slice)

> **Branch:** `newplan` · **Date:** 2026-10-07 / 2026-10-08
> **Goal:** the ESP32 keeps its full ReAct agent (context, memory, tools) but
> **stops polling Telegram directly** and **tunnels LLM inference requests** through
> the backend. The backend becomes a **cloud gateway**: it polls Telegram once, routes
> messages to the device over a persistent WebSocket, sends device replies back to Telegram,
> and relays LLM requests (`llm.request` / `llm.response`) using server-held credentials.
>
> References: `docs/ARCH_COMPARISON_DIRECT_VS_CLOUD.md` (why) ·
> `.agent/SYSTEM_OVERVIEW.md` (current architecture).

---

## 1. Architecture (Hybrid Mode)

```
Telegram Bot API ◀──── poll/send ────┐
                                     │
OpenAI-compatible LLM ◀─ relay ──────┤
                                     │
                     ┌───────────────┴──────────────────────┐
                     │  BACKEND (cloud gateway role)        │
                     │  · TelegramGateway (poller)          │
                     │  · DeviceRegistry (chat↔device)      │
                     │  · GatewaySettings (/cloud config)   │
                     │  · /ws/device  WS endpoint & relay   │
                     └───────────────┬──────────────────────┘
                                     │ ONE persistent WebSocket (device = client)
                                     │ frames: message_in / message_out / photo_out
                                     │         llm.request / llm.response
                                     ▼
┌─────────────────────────────────────────────────────────────┐
│                ESP32-S3 DEVICE (MimiClaw)                   │
│  cloud_link (WS client) ──▶ message_bus (channel "cloud")   │
│  ──▶ AGENT LOOP (context assembly, tool execution)          │
│  llm_proxy ──▶ cloud_link_llm_request() (keyless operation) │
│  reply ──▶ outbound dispatch ──▶ cloud_link_send_text()     │
└─────────────────────────────────────────────────────────────┘
```

**What changes**
| Side | Change |
|---|---|
| Firmware | `main/channels/cloud_link/` (WS client). Telegram polling disabled when `MIMI_CLOUD_LINK_ENABLE=1`. Outbound dispatch gains channel `"cloud"`. `main/llm/llm_proxy.c` tunnels inference calls through `cloud_link_llm_request` when cloud link is active. |
| Backend | `services/device_registry.py`, `services/telegram_gateway.py`, `services/gateway_settings.py`, `api/cloud_router.py` (`/ws/device`, bindings, runtime `/gateway-config`, LLM relay). Web UI `/cloud` (`static/cloud.html`). Gated by `CLOUD_GATEWAY_ENABLED`. |
| Unchanged | agent loop ReAct turns, memory/session files, local C tools (GPIO/OLED/audio/camera), voice STT/TTS, dashboard SPA, OTA, captive portal provisioning. |

## 2. WebSocket protocol v1 (JSON text frames)

**Device → Cloud**

| type | payload | notes |
|---|---|---|
| `hello` | `{"device_id","fw"}` | first frame after accept; device_id = `ESP-XXXXXXXXXXXX` (MAC) |
| `message_out` | `{"chat_id","text"}` | agent reply → Telegram sendMessage |
| `photo_out` | `{"chat_id","image_b64","caption"}` | camera snapshot → Telegram sendPhoto |
| `llm.request` | `{"request_id","body"}` | OpenAI chat completion payload; server injects Authorization header |
| `ping` | `{}` | optional keepalive |

**Cloud → Device**

| type | payload | notes |
|---|---|---|
| `hello_ack` | `{"device_id"}` | sent after registration |
| `message_in` | `{"chat_id","text","message_id","from_name"}` | routed Telegram text |
| `llm.response`| `{"request_id","ok","status","body"}` | OpenAI completion response or error string |
| `pong` | `{}` | |
| `error` | `{"detail"}` | e.g. no binding for chat |

Auth: query param `?key=<DEVICE_API_KEY>` on `/ws/device`; mismatch → close 4401.
Routing: `registry.chat_to_device[chat_id] = device_id`; auto-bind: first update
from an unbound chat binds to the **only** connected device (single-device dev
convenience); explicit REST binding also provided. Bindings persist to
`data/cloud_bindings.json`.
Runtime Config: stored in `data/gateway_config.json`, editable in `/cloud`.

## 3. Acceptance criteria (AC)

- **AC-1 Auth** — WS connect with missing/wrong `key` is closed (4401); correct key accepted and `hello_ack` received after `hello`.
- **AC-2 Inbound routing** — a Telegram update for a bound chat reaches the bound device as `message_in` within one poll interval, with chat_id/text/message_id intact.
- **AC-3 Outbound send** — a `message_out` frame triggers exactly one Telegram `sendMessage` HTTP call with the correct chat_id/text; `photo_out` triggers `sendPhoto`.
- **AC-4 Binding** — auto-bind on first update from an unbound chat (single connected device) and persistence of bindings across gateway restart (JSON file).
- **AC-5 Disconnect safety** — device disconnect cleans the registry entry; messages for a chat with no live device are dropped with a log (no crash, no leak); reconnect re-registers and receives subsequent messages.
- **AC-6 Runtime Config & Poller Restart** — gateway configuration (`telegram_bot_token`, poll interval, LLM credentials) can be inspected via `GET /api/cloud/gateway-config`, updated via `PUT`, persisted across restarts, and triggers dynamic Telegram poller reload.
- **AC-7 LLM Relay** — `llm.request` sent over WS is intercepted by the server, injected with server-configured API key and model override, dispatched to `/v1/chat/completions`, and returned to the device as `llm.response`.
- **AC-8 Web Configuration Dashboard** — dedicated dashboard at `/cloud` provides responsive controls matching `DESIGN.MD` for setting Telegram tokens, LLM API keys, testing connections, and inspecting bindings.
- **AC-9 Host E2E** — all of AC-1..AC-7 verified by host-side automated test suite (`espet-backend/tests/test_cloud_gateway.py`) using mock Telegram and mock LLM servers (10/10 PASS).
- **AC-10 Hardware E2E** *(deferred — needs build/flash)* — physical device connects, passes frames, and runs ReAct loop keylessly.

## 4. Test cases

| ID | Test | Verifies | Status |
|---|---|---|---|
| TC-01 | Connect `/ws/device` without key → connection closed | AC-1 | PASS |
| TC-02 | Connect with wrong key → closed; with right key → `hello_ack` | AC-1 | PASS |
| TC-03 | Mock Telegram returns update (chat `-100`, "hi") → bound device receives `message_in` with all fields | AC-2 | PASS |
| TC-04 | Device sends `message_out{chat_id:-100,text:"yo"}` → mock Telegram records one `sendMessage` with matching payload | AC-3 | PASS |
| TC-05 | Update from unbound chat + single connected device → auto-bind, message still delivered | AC-4 | PASS |
| TC-06 | After TC-05, `data/cloud_bindings.json` contains the mapping; a fresh registry instance loads it | AC-4 | PASS |
| TC-07 | Device disconnects → registry connection removed; update for that chat → dropped with log, gateway stays alive | AC-5 | PASS |
| TC-08 | Device sends `photo_out` with base64 JPEG bytes → mock Telegram records `sendPhoto` with matching caption | AC-3 | PASS |
| TC-09 | Gateway settings GET/PUT via REST; updates persisted to JSON file; poller updated | AC-6 | PASS |
| TC-10 | Device sends `llm.request` frame → backend injects server credentials → mock LLM completes → `llm.response` returned | AC-7 | PASS |

Run suite locally:
```bash
cd espet-backend
./venv/bin/python tests/test_cloud_gateway.py
```

## 5. Implementation Summary

1. `services/device_registry.py`: manages active WebSockets, chat bindings, and JSON persistence (`data/cloud_bindings.json`).
2. `services/telegram_gateway.py`: Telegram bot poller task with auto-bind, send message, send photo, and offset tracking.
3. `services/gateway_settings.py`: dynamic configuration store for Telegram bot token and LLM credentials with `data/gateway_config.json` persistence.
4. `api/cloud_router.py`: WebSocket endpoint `/api/cloud/ws/device`, binding management endpoints, live diagnostic test endpoints (`/test-telegram`, `/test-llm`), `/gateway-config`, and `_llm_relay`.
5. `static/cloud.html`: standalone native web configuration interface adhering to `DESIGN.MD`.
6. `main/channels/cloud_link/`: FreeRTOS WebSocket client transport supporting bidirectional messaging and synchronous `cloud_link_llm_request`.
7. `main/llm/llm_proxy.c`: transparent switching to cloud link tunnel for keyless inference when connected.
8. `espet-backend/tests/test_cloud_gateway.py`: complete host-side automated test suite with mock Telegram and mock LLM servers (10/10 passed).
