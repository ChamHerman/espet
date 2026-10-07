# AGENTS.MD — ESPET FYP Project Knowledge Base

> **Purpose**: Single source of truth for all AI agents working on this project.
> **Last updated**: 2026-07-28
> **Project root**: `c:/herman/OneDrive - Default Directory/.TARUMT/_FYP/_Antigravity/`

---

## 1. Project Identity

| Field | Value |
|-------|-------|
| **Title** | Embodied AI Agent System for Context-Aware Personalized Interaction (Embedded Sensing and Interaction Module) |
| **Course** | BMCS 3403 — Final Year Project (FYP), Tunku Abdul Rahman University of Management and Technology (TARUMT) |
| **Programme** | RSW |
| **Supervisor** | Dr Wong Thein Lai |
| **Deadline** | Final FYP Report + deliverables due **2026-12-11** (mark entry 2026-12-18) |
| **Time remaining** | ~4 months from 2026-07-28 |

### Team

| Member | Short Name | Email | Role |
|--------|-----------|-------|------|
| Cham Herman | **hm** | chamh-wm23@student.tar.edu.my | Lead developer, firmware & backend |
| Jeff Wong | **jeff** | — | Co-developer |

> 2 people must complete **all implementation + full FYP documentation** in 4 months.

---

## 2. What This Project Is

An **embodied AI agent** running on a **$5 ESP32-S3 chip** that:
1. **Senses** the real world via microphone, camera, buttons, and environmental sensors
2. **Thinks** via a ReAct agent loop (Mimiclaw/OpenClaw) that calls cloud LLMs
3. **Acts** through tools — sending Gmail, setting Google Calendar events, controlling GPIO, displaying on OLED, speaking through speaker, and more
4. **Remembers** via persistent personalized memory stored on SPIFFS flash

### Core Thesis
> Current AI assistants are trapped behind screens and require user-initiated interaction. This project bridges the gap by putting an AI agent into a physical device with real-world sensing, enabling **continuous, context-aware, and proactive** AI interaction.

---

## 3. System Architecture (High-Level)

```
┌──────────────────────────────────────────────────────────┐
│                    ESP32-S3 Device (ESPET)                │
│                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │
│  │   MIC    │  │  CAMERA  │  │  BUTTON  │  │ SENSORS │ │
│  │ INMP441  │  │  OV2640  │  │  GPIO48  │  │  (env)  │ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬────┘ │
│       │              │              │              │      │
│       ▼              ▼              ▼              ▼      │
│  ┌──────────────────────────────────────────────────┐    │
│  │          Mimiclaw Agent Loop (Core 1)            │    │
│  │   Context Builder → LLM Proxy → Tool Dispatch   │    │
│  │   SOUL.md + USER.md + MEMORY.md + Session        │    │
│  └──────────────────────┬───────────────────────────┘    │
│                         │                                │
│  ┌──────────┐  ┌───────┴──────┐  ┌──────────┐          │
│  │   OLED   │  │   SPEAKER    │  │ RGB LED  │          │
│  │ SSD1306  │  │  MAX98357A   │  │          │          │
│  └──────────┘  └──────────────┘  └──────────┘          │
└──────────────────────────┬───────────────────────────────┘
                           │ WiFi (HTTPS)
                           ▼
              ┌────────────────────────┐
              │   Cloud / Backend      │
              │                        │
              │  ┌──────────────────┐  │
              │  │  LLM Provider    │  │
              │  │  (Gemini/Claude/ │  │
              │  │   OpenAI/Local)  │  │
              │  └──────────────────┘  │
              │                        │
              │  ┌──────────────────┐  │
              │  │  Backend APIs    │  │
              │  │  (Gmail, GCal,   │  │
              │  │   MCP servers,   │  │
              │  │   custom tools)  │  │
              │  └──────────────────┘  │
              └────────────────────────┘
```

### Data Flow
1. User speaks / presses button / environment triggers sensor
2. ESP32 captures input (voice via INMP441, image via OV2640, GPIO)
3. Mimiclaw agent loop (Core 1) builds context prompt from input + SOUL + USER + MEMORY
4. Sends to cloud LLM for reasoning
5. LLM returns text response OR tool_use instructions
6. Agent executes tools (web search, GPIO control, OLED display, backend API calls)
7. Loops until `end_turn` (max 10 iterations — ReAct pattern)
8. Response sent back via Telegram / WebSocket / Speaker / OLED

---

## 4. Hardware Components

| Category | Component | Model | Purpose |
|----------|-----------|-------|---------|
| **Main Controller** | ESP32 Dev Board | **ESP32-S3 N16R8** (16MB flash, 8MB PSRAM) | WiFi, Bluetooth, AI agent host |
| **Camera** | Camera Module | **OV2640 2MP** | Image capture, vision |
| **Display** | OLED Display | **0.96" SSD1306** (128×64, I²C) | Status, text, AI responses |
| **Microphone** | Digital Mic | **INMP441 I²S MEMS** | Voice input (planned main input) |
| **Audio Amplifier** | I²S Amp | **MAX98357A** | Digital-to-analog speaker driver |
| **Speaker** | Passive Speaker | **3W 4Ω (40mm)** | Audio playback / TTS output |
| **LED** | RGB LED | **4-Pin Common Cathode** | Visual status indicator |
| **Button** | Push Button | **6×6mm Tactile** (GPIO48) | User interaction trigger |
| **Prototyping** | Breadboard | **400-Point Solderless** | Circuit assembly |

---

## 5. Codebase: `espet/` Directory

### Base: MimiClaw (fork of open-source Mimiclaw)
- **Language**: Pure C (no Linux, no Node.js)
- **Framework**: ESP-IDF v5.5+ with FreeRTOS
- **Architecture**: Dual-core — Core 0 (I/O), Core 1 (Agent Loop)

### Source Tree
```
espet/
├── main/
│   ├── mimi.c                 # Entry point (app_main)
│   ├── mimi_config.h          # All compile-time constants
│   ├── mimi_secrets.h         # API keys, WiFi creds (gitignored)
│   │
│   ├── agent/
│   │   ├── agent_loop.c/h     # ReAct loop: LLM → tool → repeat (max 10 iter)
│   │   └── context_builder.c/h # Builds system prompt from SOUL+USER+MEMORY+tools
│   │
│   ├── llm/
│   │   └── llm_proxy.c/h      # LLM API calls (Anthropic + OpenAI compatible)
│   │
│   ├── tools/
│   │   ├── tool_registry.c/h   # Tool registration + dispatch
│   │   ├── tool_web_search.c/h # Brave/Tavily web search
│   │   ├── tool_gpio.c/h       # GPIO read/write/read_all
│   │   ├── tool_oled.c/h       # OLED display control
│   │   ├── tool_cron.c/h       # Cron scheduler tools
│   │   ├── tool_files.c/h      # File read/write on SPIFFS
│   │   └── tool_get_time.c/h   # Current time via HTTP
│   │
│   ├── memory/
│   │   ├── memory_store.c/h    # MEMORY.md + daily notes (YYYY-MM-DD.md)
│   │   └── session_mgr.c/h    # JSONL per-chat session history
│   │
│   ├── channels/
│   │   ├── telegram/           # Telegram bot (long polling)
│   │   └── feishu/             # Feishu bot
│   │
│   ├── bus/                    # FreeRTOS message queues (inbound/outbound)
│   ├── cli/                    # Serial REPL console
│   ├── cron/                   # Cron scheduler service
│   ├── heartbeat/              # Periodic task checker (HEARTBEAT.md)
│   ├── gateway/                # WebSocket server (:18789)
│   ├── skills/                 # Skill loader from SPIFFS
│   ├── onboard/                # WiFi captive portal onboarding
│   ├── proxy/                  # HTTP CONNECT tunnel
│   └── ota/                    # Over-the-air firmware updates
│
├── spiffs_data/
│   ├── config/
│   │   ├── SOUL.md             # AI personality definition
│   │   └── USER.md             # User profile
│   └── skills/
│       ├── gpio-control.md     # GPIO control skill
│       ├── daily-briefing.md   # Daily briefing skill
│       ├── weather.md          # Weather skill
│       └── skill-creator.md    # Meta-skill for creating new skills
│
├── docs/
│   ├── ARCHITECTURE.md         # Full system architecture doc
│   ├── TODO.md                 # Feature gap tracker vs Nanobot reference
│   └── WIFI_ONBOARDING_AP.md   # WiFi onboarding flow
│
└── sdkconfig                   # ESP-IDF build configuration
```

### Key Configuration (mimi_config.h)
| Config | Value | Notes |
|--------|-------|-------|
| LLM Default Model | `gemma-4-e2b-it` | Currently using local LM Studio |
| LLM Provider | `openai` (OpenAI-compatible API) | Points to `http://<HOST_IP>:1234/v1/chat/completions` |
| Max Tool Iterations | 10 | ReAct loop limit |
| Max History Messages | 20 | Session context window |
| Heartbeat Interval | 30 min | Checks HEARTBEAT.md |
| Cron Check Interval | 60s | Checks cron.json |
| SPIFFS Partition | 12 MB | Markdown memory + sessions |
| WebSocket Port | 18789 | LAN access |

### Existing Tools on ESP32
| Tool | Source File | Description |
|------|-----------|-------------|
| `web_search` | tool_web_search.c | Tavily/Brave Search API |
| `get_current_time` | tool_get_time.c | Fetch time via HTTP |
| `cron_add/list/remove` | tool_cron.c | Schedule tasks |
| `gpio_read/write/read_all` | tool_gpio.c | Hardware GPIO control |
| `oled_display` | tool_oled.c | OLED text display |
| `read_file/write_file/list_dir` | tool_files.c | SPIFFS file operations |

### Memory System (SPIFFS Files)
| File | Purpose |
|------|---------|
| `SOUL.md` | AI personality — edit to change behavior |
| `USER.md` | User profile — name, preferences, language |
| `MEMORY.md` | Long-term memory — persists across reboots |
| `HEARTBEAT.md` | Task list checked every 30 min for autonomous action |
| `cron.json` | Scheduled jobs (recurring/one-shot) |
| `YYYY-MM-DD.md` | Daily notes |
| `tg_<chat_id>.jsonl` | Per-chat conversation history |

---

## 6. Current Progress (as of 2026-07-28)

### ✅ Done
- ESP32-S3 flashed with Mimiclaw firmware (pure C, FreeRTOS)
- Telegram bot integration — can send commands from Telegram
- GPIO control via Telegram (turn on/off LED on specific GPIO pins)
- OLED display control via Telegram (change display text)
- ReAct agent loop working (LLM → tool calls → loop)
- Web search tool (Brave/Tavily)
- Cron scheduler + Heartbeat service
- WebSocket gateway
- Serial CLI for debug
- WiFi onboarding captive portal
- OTA firmware updates
- Multi-LLM provider support (Anthropic + OpenAI-compatible)

### 🔲 Not Yet Done (from TODO.md gap tracker)
- **P0**: Memory write via tool use (agent can't persist memories yet)
- **P1**: Telegram allowlist (security — anyone can use the bot)
- **P1**: Telegram media handling (photos/voice/files)
- **P1**: Skills system fully integrated
- **P1**: Full bootstrap file alignment (AGENTS.md, TOOLS.md)
- **P2**: Voice transcription (Whisper API)
- Microphone (INMP441) not yet wired as main input
- Camera (OV2640) not yet integrated for vision
- Speaker (MAX98357A) not yet integrated for TTS output
- **Backend server** not yet built (Gmail, Google Calendar, MCP servers)
- **FYP Report** chapters not yet written

---

## 7. Planned Architecture (Next Phase)

### 7.1 Voice-First Input Pipeline
```
INMP441 Mic → I²S → ESP32 → [WiFi] → Speech-to-Text API → Agent Loop
                                                                  │
Agent Loop → Text-to-Speech API → [WiFi] → ESP32 → I²S → MAX98357A → Speaker
```
The microphone becomes the **primary input source**, replacing Telegram text.

### 7.2 Backend Server (To Be Built)
The ESP32 has limited processing power. It acts as an **input receiver and decision dispatcher**. Heavy work is offloaded to a backend server.

```
ESP32 (edge device)
  │
  │  API calls / MCP protocol
  ▼
┌────────────────────────────────────┐
│         Backend Server             │
│                                    │
│  ┌─────────────┐  ┌────────────┐  │
│  │  Agent       │  │  MCP       │  │
│  │  Orchestrator│  │  Servers   │  │
│  └──────┬──────┘  └─────┬──────┘  │
│         │                │         │
│  ┌──────▼────────────────▼──────┐  │
│  │        Tool Layer            │  │
│  │  • Gmail API                 │  │
│  │  • Google Calendar API       │  │
│  │  • Web Search                │  │
│  │  • File Management           │  │
│  │  • Custom Agent Tasks        │  │
│  │  • More as needed...         │  │
│  └──────────────────────────────┘  │
└────────────────────────────────────┘
```

**Design goal**: Like a miniature Hermes Agent — smaller in size, quicker in decision, simple in task execution. The ESP32 captures input, makes quick routing decisions, then delegates to the backend via API/MCP for actual execution.

### 7.3 Tool Expansion Plan
| Tool Category | Examples | Backend Required? |
|---------------|----------|-------------------|
| Communication | Send Gmail, send Telegram message | Yes |
| Scheduling | Google Calendar events, reminders | Yes |
| Information | Web search, weather, news | Yes (proxy) |
| Device Control | GPIO, OLED, LED, speaker | No (on-device) |
| Memory | Read/write persistent memory | No (SPIFFS) |
| Agent Tasks | Spawn sub-tasks, chain actions | Yes |

---

## 8. FYP Report Requirements

The FYP Report template (`FYP Report Template (updated 2.7.25).pdf`) requires:

| Chapter | Title | Deadline | Status |
|---------|-------|----------|--------|
| 1 | Introduction | 2026-06-26 | 🔲 Not started |
| 2 | Research Background (Literature Review) | 2026-07-10 | 🔲 Not started |
| 3 | Methodology and Requirements Analysis | 2026-07-24 | 🔲 Not started |
| 4 | System Design | 2026-08-14 | 🔲 Not started |
| — | Project I Portfolio submission | 2026-09-04 | 🔲 |
| — | Initial System Preview | 2026-09-11 | 🔲 |
| — | Test plan / experiment plan | 2026-11-13 | 🔲 |
| — | Final System Testing | 2026-11-20 | 🔲 |
| — | Draft FYP Report | 2026-12-04 | 🔲 |
| — | Final FYP Report | 2026-12-11 | 🔲 |

> ⚠️ Chapters 1–3 deadlines have already passed. Need to catch up immediately.

---

## 9. References (Academic)

These papers form the theoretical foundation. Full markdown conversions are available in the project root.

### Ref 1: Embodied AI Agents — Fung et al. (2025)
- **File**: `2506.22355v3.md`
- **Citation**: Fung, P., et al. (2025). "Embodied AI Agents: Modeling the World." Meta AI Research. arXiv:2506.22355
- **Key concepts**: World models for embodied agents, multimodal perception, planning through reasoning, mental models of users, wearable agents, virtual avatars, robotic agents
- **Relevance**: Foundational framework for why embodied AI needs physical sensing + world models. Supports our argument that AI must move beyond screen-based interaction.

### Ref 2: PersONAL Benchmark — Ziliotto et al. (2025)
- **File**: `2509.19843v1.md`
- **Citation**: Ziliotto, F., et al. (2025). "PersONAL: Towards a Comprehensive Benchmark for Personalized Embodied Agents." arXiv:2509.19843
- **Key concepts**: Personalized object navigation, user-specific semantics, grounding personal preferences in physical environments, active navigation in unseen environments
- **Relevance**: Directly addresses the personalization challenge — agents must model individual user preferences and behaviors. Supports our persistent memory / USER.md approach.

### Ref 3: Embodied AI for Mobile Service Robots — Lisondra et al. (2026)
- **File**: `robotics-15-00055-v2.md`
- **Citation**: Lisondra, M., Benhabib, B., & Nejat, G. (2026). "Embodied AI with Foundation Models for Mobile Service Robots: A Systematic Review." Robotics, 15(3), 55.
- **Key concepts**: Foundation models (LLMs, VLMs, MLLMs) for embodied AI, translating NL instructions to robot actions, multimodal perception, computational constraints for edge deployment, domestic assistance applications
- **Relevance**: Systematic review providing evidence base for our approach. Directly addresses computational constraints of edge deployment (ESP32) and real-world service applications.

---

## 10. Key Technical Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| MCU | ESP32-S3 N16R8 | $5-10, 16MB flash, 8MB PSRAM, dual-core, WiFi+BLE, sufficient for agent loop |
| Language | C (bare-metal, FreeRTOS) | No OS overhead, direct hardware access, fits in 2MB firmware slot |
| Agent Framework | Mimiclaw (OpenClaw fork) | Open-source, proven ReAct loop on ESP32, MIT license |
| LLM Provider | OpenAI-compatible API (currently local LM Studio with gemma-4-e2b-it) | Flexible — can switch between Gemini, Claude, GPT, local models |
| Primary Input Channel | Telegram (current) → Microphone (planned) | Telegram works now; mic is the target for embodied interaction |
| Storage | SPIFFS (12MB partition) | Flat filesystem, good enough for markdown files + JSONL sessions |
| Memory Pattern | Markdown files (SOUL.md, USER.md, MEMORY.md) | Human-readable, easy to inspect/edit, persists across reboots |

---

## 11. File Map

```
_FYP/_Antigravity/
│
├── AGENTS.MD                     ← THIS FILE (project knowledge base)
├── components.md                 ← Hardware component list
│
├── Form 2_Proposal (...).md      ← FYP proposal (markdown)
├── Form 2_Proposal (...).pdf     ← FYP proposal (original)
├── FYP Report Template (...).pdf ← Report template to follow
│
├── 2506.22355v3.md               ← Ref: Fung et al. — Embodied AI Agents
├── 2509.19843v1.md               ← Ref: Ziliotto et al. — PersONAL benchmark
├── robotics-15-00055-v2.md       ← Ref: Lisondra et al. — Foundation Models for Robots
│
└── espet/                        ← CODEBASE (Mimiclaw fork)
    ├── main/                     ← All C source code
    ├── spiffs_data/              ← Default SPIFFS files (SOUL.md, USER.md, skills)
    ├── docs/                     ← Architecture docs, TODO, onboarding
    ├── build/                    ← Build output
    └── sdkconfig                 ← ESP-IDF config
```

---

## 12. Work Breakdown (Parallel Tracks)

### Track A: Hardware + Firmware (ESP32)
1. Wire all components onto breadboard (mic, speaker, camera, OLED, LED, button)
2. Integrate INMP441 microphone as primary input (I²S → audio capture → STT)
3. Integrate MAX98357A speaker for TTS output (I²S → audio playback)
4. Integrate OV2640 camera for image capture
5. Test full sensor loop: mic → agent → speaker response

### Track B: Backend Server
1. Design backend API architecture (REST / MCP)
2. Implement core backend with agent orchestrator
3. Integrate Gmail API tool
4. Integrate Google Calendar API tool
5. Add MCP server support for extensible tool layer
6. Connect ESP32 → Backend API pipeline
7. Add more agent tools as needed

### Track C: FYP Documentation
1. **Chapter 1 — Introduction**: Background, problem statement, objectives, scope
2. **Chapter 2 — Research Background**: Literature review using the 3 reference papers
3. **Chapter 3 — Methodology**: Iterative & incremental development model, requirements analysis
4. **Chapter 4 — System Design**: Architecture diagrams, module design, data flow

### Suggested Team Split
| Person | Primary | Secondary |
|--------|---------|-----------|
| **hm** (Herman) | Track B (Backend) + Track A (firmware) | Track C (documentation) |
| **jeff** (Jeff) | Track A (Hardware wiring + testing) | Track C (documentation) |

> Both contribute to documentation. All 4 chapters + system implementation must be done by **2026-09-04** for Project I portfolio submission.

---

## 13. Agent Instructions

When working on this project:

1. **Codebase is in `espet/`** — pure C, ESP-IDF v5.5+, FreeRTOS. Do not assume Python/Node.
2. **Config is in `mimi_config.h`** — all constants. Secrets in `mimi_secrets.h` (gitignored).
3. **The agent loop is the heart** — `main/agent/agent_loop.c` implements the ReAct pattern.
4. **Tools are registered in** `main/tools/tool_registry.c` — follow existing patterns to add new tools.
5. **Memory files are markdown** — SOUL.md, USER.md, MEMORY.md on SPIFFS.
6. **LLM currently uses local LM Studio** at `http://<HOST_IP>:1234/v1/chat/completions` with `gemma-4-e2b-it`.
7. **Backend does not exist yet** — needs to be designed and built from scratch.
8. **FYP Report uses specific template** — `FYP Report Template (updated 2.7.25).pdf`.
9. **References are converted to markdown** — use the `.md` files for citation and content.
10. **Time is critical** — 4 months, 2 people, full system + full documentation.
