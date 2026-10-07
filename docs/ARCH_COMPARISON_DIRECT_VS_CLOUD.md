# ESPET Architecture Comparison — Plan A (Direct-Connect) vs Plan C (Cloud Gateway)

> **Status:** design study · **Date:** 2026-10-07
> **Scope:** compares the architecture **as built today** (Plan A) against the
> proposed **product/cloud-gateway architecture** (Plan C), with data flows,
> pros/cons, and a migration map.
>
> Read together with: [`.agent/SYSTEM_OVERVIEW.md`](../.agent/SYSTEM_OVERVIEW.md)
> (current system detail) · [`.agent/ESPET_PINS.MD`](../.agent/ESPET_PINS.MD) (wiring).

---

## 1. The two plans in one sentence

| | Plan A — Direct-Connect (current) | Plan C — Cloud Gateway (product) |
|---|---|---|
| **Idea** | The ESP32 talks to **everything directly**: it polls Telegram itself, calls the LLM itself, and uses the LAN backend only as a tool/config helper. | The ESP32 talks to **one cloud server only** (persistent WebSocket). The cloud polls Telegram, orchestrates the LLM, and pushes instructions/results to the device. |
| **Who it fits** | Single-user development / FYP demo | Multi-customer product with hosted server |

---

## 2. Plan A — Current architecture (as built)

Source of truth: `.agent/SYSTEM_OVERVIEW.md` §1. Firmware = full stack on device.

```
                    YOU
   "Hi ESP" · camera button · Telegram / Feishu / dashboard
        │
        ▼
┌─────────────────────────────────────────────────────────────┐
│                ESP32-S3 DEVICE (MimiClaw)                   │
│                                                             │
│  INPUT: INMP441 mic · buttons · Telegram long-poll          │
│  OUTPUT: speaker · Telegram / Feishu / WS replies           │
│                                                             │
│  Message Bus ──▶ AGENT LOOP (Core 1, ReAct ≤10 iters)       │
│                     1. build context (SOUL/USER/MEMORY)     │
│                     2. call LLM  ◀────────────┐             │
│                     3. tool_use? ──▶ registry  │             │
│                     4. feed result back ───────┘             │
│                                                             │
│  TOOLS: local (gpio/audio/camera/files/cron)                │
│         remote (Gmail/Calendar/Docs/Drive via backend)      │
│  SPIFFS: SOUL.md · USER.md · MEMORY.md · cron.json          │
│  DAEMONS: cron · heartbeat · config_sync (pull 300 s)       │
└──────┬──────────────────────┬──────────────────┬────────────┘
       │ HTTPS                │ HTTPS (Bearer    │ LAN HTTP
       │ long-poll every ~6 s │ device key)      │
       ▼                      ▼                  ▼
┌──────────────┐   ┌──────────────────┐   ┌─────────────────────┐
│  TELEGRAM    │   │  LLM PROVIDER    │   │  LAN BACKEND :8000  │
│  Bot API     │   │  (OpenAI-compat/ │   │  FastAPI on user PC │
│  (internet)  │   │   Anthropic)     │   │  · MCP tools/OAuth  │
└──────────────┘   └──────────────────┘   │  · config store     │
                                          │  · dashboard SPA    │
                                          │  · voice TTS/STT    │
                                          └─────────────────────┘
```

### 2.1 Plan A data flows

**Inbound Telegram message (text):**

```
User types "hi" in Telegram
  → Telegram holds it (no push to bots)
  → ESP32 getUpdates long-poll (fresh TLS conn every ~6 s) fetches it
  → message_bus → agent loop → context build → LLM call (2nd connection)
  → [optional tool calls → 3rd connection to LAN backend / internet]
  → outbound queue → sendMessage (4th connection)
  → optional TTS fetch from LAN backend (5th connection) + speaker
```

**Camera button (local, no cloud):**

```
Short press GPIO 41 → OV2640 snapshot → multipart upload to Telegram
Hold ≥600 ms        → JPEG frames → PSRAM → MJPEG AVI → sendVideo
```

**Voice (wake word):**

```
"Hi ESP" → WakeNet on-chip (offline) → ack TTS/beep → record PCM
→ POST to LAN backend /api/voice/transcribe (Whisper)
→ agent loop as above → reply text → /api/voice/tts → speaker
```

---

## 3. Plan C — Cloud Gateway (product form)

The device keeps **everything physical and real-time**; it loses the Telegram
and LLM channel adapters and gains **one persistent outbound WebSocket** to the
hosted cloud. The cloud becomes the single internet-facing brain.

```
                    YOU (customer)
   "Hi ESP" · camera button · Telegram · product web/app dashboard
        │
        ▼
┌─────────────────────────────────────────────────────────────┐
│                ESP32-S3 DEVICE (MimiClaw firmware)          │
│                                                             │
│  KEEP — the whole agent stays on-device:                    │
│    Message Bus ─▶ AGENT LOOP (Core 1, ReAct ≤10 iters)      │
│      1. build context (SOUL/USER/MEMORY/skills — SPIFFS)    │
│      2. LLM call      ── transport changes, see below       │
│      3. tool_use? ──▶ tool registry (local + cloud tools)   │
│      4. feed result back ──▶ repeat                         │
│  KEEP: mic + offline WakeNet · speaker · camera · buttons    │
│        OLED · SPIFFS memory · session history · cron        │
│        heartbeat · OTA · provisioning portal                │
│        local tools (gpio/audio/camera/files)                │
│                                                             │
│  CHANGED: llm_proxy.c transport — the LLM request now rides  │
│           the cloud tunnel (WS frame) instead of direct     │
│           HTTPS; agent logic, prompts, and memory unchanged │
│  REMOVE: telegram_bot.c direct polling (cloud does it)      │
│                                                             │
│  ADD: cloud_link — ONE persistent outbound WSS connection   │
│       (device is the CLIENT → works behind any NAT/CGNAT)   │
└──────────────────────────┬──────────────────────────────────┘
                           │ WSS (outbound, always-on, device-key auth)
                           │  ↑ pushed: commands, replies, config, OTA
                           │  ↓ sent: audio, photos, events, status,
                           │        llm.request / llm.response frames
                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    CLOUD SERVER  (you host, per product)            │
│                                                                     │
│  Device Registry ── device_id ↔ customer account ↔ live connections │
│  Telegram Gateway ── ONE poller per bot; routes chat_id ↔ device    │
│  LLM Gateway     ── provider routing, keys, billing, streaming      │
│                     (relay for the device's agent — the cloud does  │
│                      NOT run its own agent loop)                    │
│  Tool/MCP Service ── Google OAuth, per-user token store             │
│  Config Service  ── per-device config, pushed instantly via WS      │
│  Voice Service   ── STT/TTS (faster-whisper, edge-tts)              │
│  OTA / Fleet     ── firmware rollout, telemetry, health             │
│  Customer Web/App ── settings, pairing, billing                     │
└───────┬─────────────────────────────────────────────────────────────┘
        │ HTTPS
        ▼
   TELEGRAM BOT API          LLM PROVIDERS         GOOGLE MCP
   (one gateway, N users)    (keys live here)      (OAuth here)
```

> **Key design decision:** the ReAct agent loop, prompt building, and memory
> stay **on the ESP32** in Plan C. The cloud is a *thin* gateway — it relays
> LLM traffic (so keys/billing/model choice stay server-side) and routes
> messages, but it does **not** reimplement the agent. This keeps the product's
> differentiator (a real embodied agent that degrades gracefully offline)
> and keeps the cloud cheap and stateless per turn.

### 3.1 Plan C data flows

**Inbound Telegram message:**

```
User types "hi" in Telegram
  → Cloud Telegram Gateway fetches it (poller on the server — no ESP32 limits)
  → Registry: chat_id → device ESP-9F8E (Xiao Hong) → her live WS connection
  → push "message_in" frame down that connection (LAN/internet, ms latency)
  → device agent loop runs (context/memory stay on device)
  → device calls LLM via cloud ("llm.request" frame) — OR cloud does it
  → device pushes final text up → cloud → Telegram sendMessage
```

**Voice:**

```
"Hi ESP" → WakeNet on-chip (unchanged, offline) → record PCM
→ stream/upload via the SAME WS connection → cloud STT → cloud LLM
→ cloud pushes TTS audio (or text + TTS-on-device choice) → speaker
```

**Camera:**

```
Button → snapshot on device → push JPEG up via WS
→ cloud attaches it to Telegram (sendPhoto/sendVideo) — or runs a VLM first
```

**Config change ("customer switches LLM"):**

```
Customer edits Settings in web dashboard → cloud updates registry
→ pushes new config down that device's WS → effective in <1 s
(Plan A equivalent: device pulls every 300 s from the LAN backend)
```

---

## 4. Advantages / Disadvantages

### 4.1 Plan A — Direct-Connect

**Advantages**

1. **Zero cloud dependency** — the device works with nothing but Wi-Fi: Telegram and LLM are reached directly; the LAN backend is optional (tools/config/dashboard), not a life-support.
2. **Already built and debugged** — full channel adapters, ReAct loop, OTA, portal, config sync are shipped and field-tested (this whole repo).
3. **No server cost** for a single user — nothing to host, nothing to pay monthly.
4. **Simpler mental model** for a one-device FYP: everything is in one codebase you can trace line by line.
5. **Privacy option** — with a local LM Studio endpoint, text never leaves the LAN; memory lives only on the device's SPIFFS.
6. **Works without any account/pairing** — flash and go.

**Disadvantages**

1. **Bot token baked into firmware** — shipping this as a product leaks the token to every customer; no per-device revocation.
2. **One bot token = one poller** — Telegram forbids concurrent `getUpdates` consumers, so N devices cannot share a bot; multi-user is impossible without a gateway anyway.
3. **Connection churn** — every poll/send/LLM call is a fresh TLS connection; on a constrained TCP stack this caused real field failures (lwIP socket starvation: 10 sockets vs ~20 TIME_WAIT slots needed). Fixed, but the pattern stays fragile on embedded stacks.
4. **Pickup latency** — messages wait up to one poll cycle (~6 s worst case, ~3 s average) before the device even sees them.
5. **Per-device keys** for LLM/Google must be provisioned onto each device; rotation means reflashing or complex remote-config paths.
6. **No fleet management** — updating 100 devices means reaching 100 home networks; no central health/telemetry story beyond one-off logs.
7. **Customer experience burden** — each customer needs their own bot (BotFather), own LLM key, own backend PC for tools; enormous friction for a consumer product.

### 4.2 Plan C — Cloud Gateway

**Advantages**

1. **True multi-tenant product** — one bot, one LLM account, N devices; the registry routes `chat_id ↔ device_id`. Keys live only on the server.
2. **Fastest message pickup** — the gateway polls aggressively (no embedded limits) and pushes over the already-open WS: worst case ~1 s vs ~6 s.
3. **Device network surface shrinks to one connection** — no more per-request TLS churn on the ESP32; the class of embedded-network failures (socket starvation, DNS flaps, NAT issues observed in the field) disappears structurally. The Mac/PC-grade server handles Telegram's quirks.
4. **Instant config & fleet** — settings, model switches, OTA, and telemetry ride the same WS; a customer changing LLM takes effect in <1 s; you can update 1000 devices from one console.
5. **Business-ready** — subscription billing, per-user tool OAuth, usage metering, device pairing via QR: all natural server-side features. Customers never touch API keys.
6. **Better raw materials** — Whisper/TTS/LLM run on server-grade hardware; the ESP32 keeps only what it is good at (real-time audio, camera, UI).
7. **Offline-degraded, not offline-dead** — wake word, buttons, camera, local memory still work when the internet drops; the device just queues what needs the cloud.

**Disadvantages**

1. **Hard dependency on your server** — if the cloud is down (or you stop paying for it), every customer's Telegram path dies; device-local features (buttons, camera, wake word) still work but the assistant is mute. SLA/uptime becomes *your* responsibility.
2. **Recurring server cost** — hosting + bandwidth + LLM tokens; must be covered by pricing (or BYO-key mode).
3. **Engineering effort** — new cloud services (gateway, registry, WS hub, billing hooks) and firmware changes (`cloud_link` channel replacing `telegram_bot.c` + `llm_proxy.c` direct calls). Realistic estimate: the WS protocol + gateway is the core work; the rest of the firmware is untouched.
4. **Latency is not free of the LLM** — C only removes the transport delay (~2–4 s average). The dominant reply time remains the LLM itself (3–10 s); a slow model is slow in both plans.
5. **Privacy/data residency** — voice, photos, and conversation now transit your server; a privacy policy and encryption-at-rest story become mandatory (in Plan A the device can stay fully local).
6. **More moving parts to debug** — device↔cloud↔Telegram↔LLM: one more hop means correlation IDs and server logging become necessary.
7. **WS connection upkeep on the ESP32** — keep-alive pings, backoff reconnect, auth-token refresh on the device; modest but new code (the existing `ws_server.c` is the inverse role — a good reference, not a drop-in client).

---

## 5. Side-by-side summary

| Dimension | Plan A (current) | Plan C (cloud gateway) |
|---|---|---|
| Message pickup latency | ~3 s avg, 6.3 s worst | ~0–1 s |
| Total reply latency | dominated by LLM (3–10 s) | dominated by LLM (3–10 s) |
| Multi-user / multi-device | ❌ (1 bot = 1 poller) | ✅ by design |
| API key handling | baked per device | server-only, customer never sees keys |
| LLM / model switching | reflash or 300 s config pull (LAN only) | instant push, any device |
| Embedded network fragility | exposed (field-verified) | structurally removed |
| Offline behavior | Telegram + LLM dead; local tools OK | cloud features dead; local tools OK |
| Server to host & pay for | none (optional LAN PC) | required, always on |
| Privacy (voice/text) | can stay fully local | transits your cloud |
| Fleet / OTA / telemetry | manual per device | built-in |
| Customer onboarding friction | high (bot + keys + backend PC) | low (pair by QR, subscribe) |
| Engineering effort | already done | gateway + registry + device WS client |
| Fits current stage (FYP) | ✅ ideal | premature |
| Fits product stage | ❌ blockers 1–2 above | ✅ the standard architecture |

---

## 6. Migration map — what actually changes

### Firmware (`main/`)

| Component | Plan A | Plan C |
|---|---|---|
| `channels/telegram/` | polls + sends directly | **replaced** by `channels/cloud_link/` (WSS client: connect, auth, reconnect/backoff, frame codec) |
| `llm/llm_proxy.c` | calls provider directly | **kept as a module** but transport becomes an `llm.request` WS frame to the cloud (or kept direct in BYO-key mode) |
| `agent/`, `memory/`, `skills/`, `tools/` (local), `cron/`, `heartbeat/` | unchanged | unchanged |
| `gateway/ws_server.c` | LAN debug server | becomes the **reference** for the cloud-link client (frame format can be reused); product device runs a WS **client** |
| `sync/config_sync.c` | HTTP pull every 300 s | config arrives **pushed** on the WS; pull kept as boot-time fallback |
| `voice/voice_client.c` | HTTP to LAN backend | STT/TTS frames over the WS (backend service unchanged, URL becomes the cloud) |
| `onboard/` portal | Wi-Fi + backend IP | Wi-Fi + **pairing code** (device claims itself to the customer account) |
| `ota/` | manual flash / existing OTA | cloud-triggered rollout |

### Backend → Cloud

| Existing (`espet-backend/`) | Becomes |
|---|---|
| FastAPI app, MCP router, OAuth store, config store, dashboard | the cloud services almost as-is (multi-tenant: key every table by `device_id`/`user_id`) |
| — (new) | Telegram Gateway: single poller per bot, `chat_id → device_id` routing, sendAPI for device replies |
| — (new) | WS Hub: live device connections, auth, push fan-out, presence |
| — (new) | Device Registry + pairing (QR claim), billing hooks |

### What is deliberately reusable

The ReAct agent loop, tool registry, memory/session design, voice pipeline
contract (`/api/voice/*`), config schema, dashboard — all survive the move.
The migration is a **channel swap + a registry**, not a rewrite.

---

## 7. Recommendation

1. **Now (FYP): stay on Plan A.** It is built, stable after the socket fixes,
   and demo-friendly. Direct Telegram is also a nice architectural story to
   defend ("the device is a full agent even without a cloud").
2. **Product phase: move to Plan C**, skipping the "Mac-as-relay" intermediate
   state — go straight to the hosted cloud gateway, since the Mac backend code
   lifts into it unchanged.
3. **In both phases, keep the ReAct loop and memory on-device.** It is the
   product's differentiator (a real embodied agent, not a dumb speaker) and it
   keeps the cloud thin (gateway + tools, not a full agent reimplementation).
4. Optional middle ground if a demo needs lower latency without the cloud:
   raise the poll cadence (a fixed-cost tweak) or send from the LAN backend —
   but these are stopgaps, not the product path.
