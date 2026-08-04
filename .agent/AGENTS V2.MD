# AGENTS V2.MD — ESPET FYP Project Knowledge Base

> **Version**: 2.0 (supersedes AGENTS.MD v1)
> **Last updated**: 2026-07-28
> **Change from v1**: Finalized two-module split — different research domains, different proposal titles, individual tech stacks defined.
> **Project root**: `c:/herman/OneDrive - Default Directory/.TARUMT/_FYP/_Antigravity/`

---

## 1. Project Identity

| Field | Value |
|-------|-------|
| **Shared Product** | ESPET — Embodied AI Agent on ESP32-S3 |
| **Course** | BMCS 3403 — Final Year Project (FYP), Tunku Abdul Rahman University of Management and Technology (TARUMT) |
| **Programme** | RSW |
| **Supervisor** | Dr Wong Thein Lai |
| **Deadline** | Final FYP Report + deliverables due **2026-12-11** (mark entry 2026-12-18) |
| **Time remaining** | ~4 months from 2026-07-28 |

### Team & Individual FYP Titles

| Member | Short Name | Email | Module | FYP Title |
|--------|-----------|-------|--------|-----------|
| Cham Herman | **hm** | chamh-wm23@student.tar.edu.my | **Ambient Voice Interaction Module** | *Embodied AI Agent System for Proactive Voice-Based Personal Assistance* |
| Jeff Wong | **jeff** | — | **Visual Context Intelligence Module** | *Embodied AI Agent System for Vision-Grounded Contextual Task Assistance* |

> **Same physical device, two completely separate FYP reports.** Both members work on both hardware AND software — differentiated by research domain, use case, and which hardware subsystem they own.

---

## 2. Why Two Separate Research Domains (Supervisor Rationale)

The original split (Sensing Module vs Intelligence Module) was rejected because:
- Hardware integration alone is not sufficient FYP contribution
- One person doing "just hardware wiring" vs one doing all software = unequal academic depth

**The fix**: Both members now own a full hardware pipeline + full software pipeline + full documentation, but serve **genuinely different research questions** with different academic literature, evaluation criteria, and system design emphasis.

| | **hm** | **jeff** |
|---|---|---|
| **Research Domain** | Ambient intelligence + Voice HCI + Proactive agents | Computer vision + Multimodal AI + Contextual assistance |
| **Core Research Question** | Can an always-on embedded device proactively assist users via voice without screen interaction? | Can an embedded device understand physical visual context to deliver relevant AI assistance? |
| **Hardware Owned** | INMP441 mic + MAX98357A speaker (I2S audio pipeline) | OV2640 camera + SSD1306 OLED (visual I/O pipeline) |
| **Agent Behavior** | **Proactive** — heartbeat/cron fires briefings, reminders, alerts | **Reactive** — button press captures scene, agent interprets and responds |
| **Backend Tools** | Gmail, Google Calendar, reminder scheduler, weather | VLM image analysis, OCR, web search, note capture |
| **Ch1 Problem** | AI assistants are screen-bound and passive; no ambient voice AI exists in physical form | AI is blind to physical workspace; knowledge workers cannot get contextual help about real materials |
| **Ch2 Literature** | Proactive agents, ambient computing, voice interfaces, wearable/embedded AI | VLMs, multimodal AI, visual question answering, AI-assisted learning |
| **Evaluation** | Voice accuracy, reminder precision, proactive trigger quality, end-to-end latency | Scene understanding accuracy, visual context relevance, task assistance quality |

---

## 3. Shared Hardware (One Device, Dual Purpose)

Both members work on the **same physical ESP32-S3 breadboard**. All components are wired. Each member owns integration responsibility for their subsystem.

| Category | Component | Model | Protocol | Owner |
|----------|-----------|-------|----------|-------|
| **Main Controller** | ESP32 Dev Board | ESP32-S3 N16R8 (16MB flash, 8MB PSRAM) | — | Shared |
| **Microphone** | Digital Mic | INMP441 I2S MEMS | I2S | **hm** |
| **Audio Amplifier** | I2S Amp | MAX98357A | I2S | **hm** |
| **Speaker** | Passive Speaker | 3W 4 Ohm 40mm | via MAX98357A | **hm** |
| **Camera** | Camera Module | OV2640 2MP | SCCB (I2C-like) | **jeff** |
| **Display** | OLED Display | 0.96" SSD1306 (128x64) | I2C | **jeff** |
| **Indicator** | RGB LED | 4-Pin Common Cathode | GPIO | Shared |
| **Input** | Push Button | 6x6mm Tactile | GPIO48 | Shared |
| **Prototyping** | Breadboard | 400-Point Solderless | — | Shared |

> If budget allows, duplicate the hardware set so both members can develop independently in parallel.

---

## 4. System Architecture

### 4.1 Shared High-Level Architecture

```
+--------------------------------------------------------------+
|                    ESP32-S3 Device (ESPET)                   |
|                                                              |
|  [hm subsystem]                [jeff subsystem]              |
|  +----------+  +----------+   +----------+  +----------+    |
|  | INMP441  |  | MAX98357A|   | OV2640   |  | SSD1306  |    |
|  |   MIC    |  | SPEAKER  |   |  CAMERA  |  |   OLED   |    |
|  +----+-----+  +----^-----+   +----+-----+  +----^-----+    |
|       | I2S         | I2S          | SCCB         | I2C      |
|       v             |              v              |          |
|  +--------------------------------------------------+        |
|  |           Mimiclaw Agent Loop (Core 1)           |        |
|  |   Context Builder -> LLM Proxy -> Tool Dispatch  |        |
|  |   SOUL.md + USER.md + MEMORY.md + Skills         |        |
|  +----------------------------+---------------------+        |
|                               | WiFi (HTTP/HTTPS)            |
|  [Shared]  RGB LED   Button   |                              |
+-------------------------------+------------------------------+
                                v
               +--------------------------------+
               |      Local Backend Server      |
               |                                |
               |  +----------+  +-----------+  |
               |  |  hm Stack|  |jeff Stack |  |
               |  |          |  |           |  |
               |  | Whisper  |  | Moondream |  |
               |  | STT      |  | VLM       |  |
               |  | Piper    |  | (Ollama)  |  |
               |  | TTS      |  |           |  |
               |  +-----+----+  +-----+-----+  |
               |        +----------+            |
               |                   v            |
               |  +--------------------------------+  |
               |  | LM Studio (Shared)             |  |
               |  | gemma-4-e2b-it                 |  |
               |  | (OpenAI-compatible API)        |  |
               |  +--------------------------------+  |
               |                                |
               |  +--------------------------------+  |
               |  | MCP Tool Servers               |  |
               |  | Gmail / GCal (hm)              |  |
               |  | Search / OCR / Notes (jeff)    |  |
               |  +--------------------------------+  |
               +--------------------------------+
```

### 4.2 hm Voice Pipeline

```
[INMP441 Mic]
     | I2S (DMA capture)
     v
ESP32 audio buffer
     | HTTP POST (PCM/WAV)
     v
Server: faster-whisper  -->  transcript text
     v
LM Studio ReAct agent loop
     | tool calls: Gmail, GCal, reminder, weather
     v
Server: Piper TTS  -->  WAV audio
     v
ESP32 audio buffer
     | I2S (DMA playback)
     v
[MAX98357A -> Speaker]
```

**Proactive flow (heartbeat/cron):**
```
Cron fires (e.g., 7:00 AM)
  -> Agent reads HEARTBEAT.md / calendar events
  -> Constructs morning briefing text
  -> Piper TTS -> Speaker plays briefing aloud
```

### 4.3 jeff Vision Pipeline

```
[OV2640 Camera]
     | JPEG capture (QVGA 320x240)
     v
ESP32 image buffer (PSRAM)
     | HTTP POST (JPEG bytes)
     v
Server: Moondream2 / LLaVA (via Ollama)
     | image description / visual Q&A answer
     v
LM Studio ReAct agent loop
     | tool calls: web search, OCR, note capture
     v
Final text response
     | I2C render
     v
[SSD1306 OLED -- display response]
  (+ optional speaker via shared TTS)
```

**Reactive flow (button trigger):**
```
Button press (GPIO48)
  -> Camera capture JPEG
  -> POST to VLM server
  -> Description injected into agent context
  -> Agent reasons + responds
  -> OLED displays result
```

---

## 5. Local-First Tech Stack (Free, Self-Hosted)

All backend services run on a local server. No paid cloud APIs required.

### hm Services

| Service | Tool | Notes |
|---------|------|-------|
| **STT** | faster-whisper | OpenAI Whisper weights, CPU/GPU, Chinese+English, expose as REST |
| **TTS** | Piper TTS (Mozilla) | Very fast on CPU, high quality voices, WAV output, free |
| **LLM** | LM Studio (gemma-4-e2b-it) | Already running, OpenAI-compatible at http://HOST:1234/v1 |
| **Tools** | Python MCP servers | Gmail API, Google Calendar API, weather, reminder scheduler |

### jeff Services

| Service | Tool | Notes |
|---------|------|-------|
| **VLM** | Moondream2 via Ollama | 1.8B params, fast on CPU/GPU, image captioning + visual Q&A, free |
| **Alt VLM** | LLaVA via Ollama | Larger, more capable, use if server GPU allows |
| **LLM** | LM Studio (gemma-4-e2b-it) | Shared with hm |
| **Tools** | Python MCP servers | Web search (SearXNG/Tavily), OCR (Tesseract), note capture, Pomodoro |

### Shared Infrastructure

| Component | Tool |
|-----------|------|
| MCP server framework | Python (FastMCP) or Node MCP SDK |
| Agent loop | Mimiclaw on ESP32 (C, FreeRTOS) |
| LLM API | LM Studio at http://HOST_IP:1234/v1/chat/completions |
| Memory | SPIFFS on ESP32 (SOUL.md, USER.md, MEMORY.md) |
| Session history | JSONL on SPIFFS per chat |

---

## 6. Codebase: espet/ Directory

### Base: MimiClaw (fork of open-source Mimiclaw)
- **Language**: Pure C (no Linux, no Node.js)
- **Framework**: ESP-IDF v5.5+ with FreeRTOS
- **Architecture**: Dual-core — Core 0 (I/O), Core 1 (Agent Loop)

### Source Tree

```
espet/
  main/
    mimi.c                  # Entry point (app_main)
    mimi_config.h           # All compile-time constants
    mimi_secrets.h          # API keys, WiFi creds (gitignored)
    agent/
      agent_loop.c/h        # ReAct loop: LLM -> tool -> repeat (max 10 iter)
      context_builder.c/h   # Builds system prompt from SOUL+USER+MEMORY+tools
    llm/
      llm_proxy.c/h         # LLM API calls (Anthropic + OpenAI compatible)
    tools/
      tool_registry.c/h     # Tool registration + dispatch
      tool_web_search.c/h   # Brave/Tavily web search
      tool_gpio.c/h         # GPIO read/write/read_all
      tool_oled.c/h         # OLED display control
      tool_cron.c/h         # Cron scheduler tools
      tool_files.c/h        # File read/write on SPIFFS
      tool_get_time.c/h     # Current time via HTTP
    memory/
      memory_store.c/h      # MEMORY.md + daily notes (YYYY-MM-DD.md)
      session_mgr.c/h       # JSONL per-chat session history
    channels/
      telegram/             # Telegram bot (long polling) -- current input
      feishu/               # Feishu bot
    bus/                    # FreeRTOS message queues (inbound/outbound)
    cli/                    # Serial REPL console
    cron/                   # Cron scheduler service
    heartbeat/              # Periodic task checker (HEARTBEAT.md)
    gateway/                # WebSocket server (:18789)
    skills/                 # Skill loader from SPIFFS
    onboard/                # WiFi captive portal onboarding
    proxy/                  # HTTP CONNECT tunnel
    ota/                    # Over-the-air firmware updates
  spiffs_data/
    config/
      SOUL.md               # AI personality definition
      USER.md               # User profile
    skills/
      gpio-control.md
      daily-briefing.md
      weather.md
      skill-creator.md
  docs/
    ARCHITECTURE.md
    TODO.md
    WIFI_ONBOARDING_AP.md
  sdkconfig
```

### Key Configuration (mimi_config.h)

| Config | Value | Notes |
|--------|-------|-------|
| LLM Default Model | gemma-4-e2b-it | Local LM Studio |
| LLM Provider | openai (OpenAI-compatible) | http://HOST_IP:1234/v1/chat/completions |
| Max Tool Iterations | 10 | ReAct loop limit |
| Max History Messages | 20 | Session context window |
| Heartbeat Interval | 30 min | Checks HEARTBEAT.md |
| Cron Check Interval | 60s | Checks cron.json |
| SPIFFS Partition | 12 MB | Markdown memory + sessions |
| WebSocket Port | 18789 | LAN access |

### Existing Tools (Already on ESP32)

| Tool | Source File | Description |
|------|-------------|-------------|
| web_search | tool_web_search.c | Tavily/Brave Search API |
| get_current_time | tool_get_time.c | Fetch time via HTTP |
| cron_add/list/remove | tool_cron.c | Schedule tasks |
| gpio_read/write/read_all | tool_gpio.c | Hardware GPIO control |
| oled_display | tool_oled.c | OLED text display |
| read_file/write_file/list_dir | tool_files.c | SPIFFS file operations |

### Tools To Build (New)

| Tool | Owner | Backend | Description |
|------|-------|---------|-------------|
| voice_transcribe | hm | faster-whisper server | Send audio buffer, get transcript text |
| speak | hm | Piper TTS server | Send text, play WAV on speaker |
| gmail_send | hm | Gmail MCP server | Send email via Gmail API |
| calendar_add | hm | Google Calendar MCP | Add event to Google Calendar |
| calendar_check | hm | Google Calendar MCP | Check upcoming events |
| camera_capture | jeff | On-device | Capture JPEG from OV2640 |
| vision_describe | jeff | Moondream/LLaVA server | Send image, get text description |
| vision_ask | jeff | Moondream/LLaVA server | Ask question about captured image |
| note_save | jeff | Local file MCP | Save note to file with timestamp |

### Memory System (SPIFFS Files)

| File | Purpose |
|------|---------|
| SOUL.md | AI personality -- edit to change behavior |
| USER.md | User profile -- name, preferences, language |
| MEMORY.md | Long-term memory -- persists across reboots |
| HEARTBEAT.md | Task list checked every 30 min (critical for hm proactive flow) |
| cron.json | Scheduled jobs (recurring/one-shot) |
| YYYY-MM-DD.md | Daily notes |
| tg_CHATID.jsonl | Per-chat conversation history |

---

## 7. Current Progress (as of 2026-07-28)

### Done (Shared Foundation)
- ESP32-S3 flashed with Mimiclaw firmware (pure C, FreeRTOS)
- Telegram bot -- GPIO LED control, OLED display text change confirmed working
- ReAct agent loop working (LLM -> tool calls -> loop)
- Web search tool (Brave/Tavily)
- Cron scheduler + Heartbeat service
- WebSocket gateway, Serial CLI, WiFi onboarding, OTA updates
- Multi-LLM provider support (Anthropic + OpenAI-compatible)

### hm -- Not Yet Done
- INMP441 mic wiring + I2S capture firmware
- MAX98357A speaker wiring + I2S playback firmware
- faster-whisper server setup + REST API endpoint
- Piper TTS server setup + REST API endpoint
- voice_transcribe + speak tools in tool_registry
- Gmail MCP server
- Google Calendar MCP server
- Memory write via tool use (agent can persist memories)
- Telegram allowlist (security)
- FYP Report Chapter 1 to 4

### jeff -- Not Yet Done
- OV2640 camera wiring + SCCB init firmware
- SSD1306 OLED wiring (I2C already partially done via tool_oled.c)
- Ollama + Moondream2 setup + REST API endpoint
- camera_capture + vision_describe + vision_ask tools
- Web search / OCR / note-capture MCP servers
- FYP Report Chapter 1 to 4

---

## 8. FYP Report Requirements

Two separate reports. Same template: FYP Report Template (updated 2.7.25).pdf

| Milestone | Date | hm Status | jeff Status |
|-----------|------|-----------|-------------|
| Chapter 1: Introduction | 2026-06-26 | OVERDUE | OVERDUE |
| Chapter 2: Research Background | 2026-07-10 | OVERDUE | OVERDUE |
| Chapter 3: Methodology & Requirements | 2026-07-24 | OVERDUE | OVERDUE |
| Chapter 4: System Design | 2026-08-14 | Not started | Not started |
| Project I Portfolio Submission | 2026-09-04 | Not started | Not started |
| Initial System Preview | 2026-09-11 | Not started | Not started |
| Test Plan | 2026-11-13 | Not started | Not started |
| Final System Testing | 2026-11-20 | Not started | Not started |
| Draft FYP Report | 2026-12-04 | Not started | Not started |
| Final FYP Report | 2026-12-11 | Not started | Not started |

> Chapters 1-3 are already overdue. Both must prioritize writing immediately in parallel with implementation.

### hm Report Outline Direction

```
Title:   Embodied AI Agent System for Proactive Voice-Based Personal Assistance
Module:  Ambient Voice Interaction Module

Ch1 Problem:
  AI assistants are passive and screen-bound. No ambient, hands-free,
  proactively helpful AI exists in physical embedded form.

Ch1 Objective:
  Build an ESP32-S3 device with voice I/O that proactively assists with
  personal life tasks without requiring a screen or manual trigger.

Ch2 Literature:
  - Proactive AI agents (anticipatory computing)
  - Ambient intelligence (AmI) frameworks
  - Voice interface design + speech HCI
  - Wearable / embedded AI assistants
  - Fung et al. (embodied AI world models) [2506.22355v3.md]
  - Lisondra et al. (foundation models on edge devices) [robotics-15-00055-v2.md]
  - Additional: voice HCI papers, proactive agent papers (find independently)

Ch3 Methodology:  Iterative & Incremental Development Model
  Phase 1: Hardware (mic + speaker I2S pipeline)
  Phase 2: STT/TTS server integration (faster-whisper + Piper)
  Phase 3: Proactive agent tools (Gmail, Calendar, reminders)
  Phase 4: Evaluation (voice accuracy, latency, proactive trigger precision)

Ch4 System Design:
  - Audio pipeline: I2S -> HTTP -> Whisper -> LLM -> Piper -> I2S
  - Proactive agent flow: heartbeat/cron -> briefing -> TTS -> speaker
  - Backend MCP tool design: Gmail, GCal, reminder
  - SOUL.md tuned for voice-first proactive personality
```

### jeff Report Outline Direction

```
Title:   Embodied AI Agent System for Vision-Grounded Contextual Task Assistance
Module:  Visual Context Intelligence Module

Ch1 Problem:
  AI is blind to physical workspace. Knowledge workers studying or working
  with physical materials (books, notes, whiteboards) cannot get contextual
  AI assistance relevant to what they are physically looking at.

Ch1 Objective:
  Build an ESP32-S3 device with camera + OLED that captures visual context
  and delivers relevant AI assistance for knowledge and study tasks.

Ch2 Literature:
  - Vision-Language Models (VLMs): LLaVA, Moondream, GPT-4V evaluations
  - Visual Question Answering (VQA) benchmarks
  - Multimodal AI and grounded language understanding
  - AI-assisted learning + cognitive load reduction theory
  - Fung et al. (embodied AI world models) [2506.22355v3.md]
  - Ziliotto et al. (PersONAL benchmark) [2509.19843v1.md]
  - Additional: VLM papers, educational AI papers (find independently)

Ch3 Methodology:  Iterative & Incremental Development Model
  Phase 1: Hardware (camera capture + OLED display pipeline)
  Phase 2: VLM server integration (Moondream/LLaVA via Ollama)
  Phase 3: Contextual agent tools (web search, OCR, note capture)
  Phase 4: Evaluation (scene understanding accuracy, response relevance)

Ch4 System Design:
  - Visual pipeline: OV2640 -> JPEG -> HTTP -> VLM -> LLM -> OLED
  - Reactive agent flow: button -> capture -> describe -> reason -> display
  - Backend MCP tool design: search, OCR, notes
  - SOUL.md tuned for knowledge-task aware personality
```

---

## 9. References (Academic)

Full markdown conversions available in project root.

### Ref 1: Fung et al. (2025) -- Embodied AI Agents
- **File**: 2506.22355v3.md
- **Citation**: Fung, P., et al. (2025). Embodied AI Agents: Modeling the World. Meta AI Research. arXiv:2506.22355
- **Used by**: Both hm and jeff (foundational embodied AI argument)
- **Key for**: Physical embodiment justification, world models, multimodal perception, wearable agents

### Ref 2: Ziliotto et al. (2025) -- PersONAL Benchmark
- **File**: 2509.19843v1.md
- **Citation**: Ziliotto, F., et al. (2025). PersONAL: Towards a Comprehensive Benchmark for Personalized Embodied Agents. arXiv:2509.19843
- **Used by**: jeff (personalization in embodied agents), hm (persistent memory angle)
- **Key for**: Personalized agent behavior, user-specific memory, grounding preferences

### Ref 3: Lisondra et al. (2026) -- Foundation Models for Robots
- **File**: robotics-15-00055-v2.md
- **Citation**: Lisondra, M., Benhabib, B., & Nejat, G. (2026). Embodied AI with Foundation Models for Mobile Service Robots: A Systematic Review. Robotics, 15(3), 55.
- **Used by**: Both hm and jeff (edge deployment, computational constraints)
- **Key for**: Foundation models on constrained hardware, real-world deployment, domestic assistance

> Both members must find additional domain-specific references independently:
> hm: voice HCI, proactive/anticipatory agent papers
> jeff: VLM benchmarks, visual QA, AI in education papers

---

## 10. Key Technical Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| MCU | ESP32-S3 N16R8 | $5-10, 16MB flash, 8MB PSRAM, dual-core, WiFi, I2S + I2C + SCCB support |
| Firmware language | C (ESP-IDF + FreeRTOS) | No OS overhead, direct hardware access, proven with Mimiclaw |
| Agent framework | Mimiclaw (OpenClaw fork) | Open-source ReAct loop on ESP32, MIT license |
| LLM backend | LM Studio (local, OpenAI-compat) | Free, local, swappable model (gemma-4-e2b-it currently) |
| STT (hm) | faster-whisper (local server) | Free, accurate, Chinese+English, runs on own hardware |
| TTS (hm) | Piper TTS (local server) | Free, fast on CPU, good quality, no cloud needed |
| VLM (jeff) | Moondream2 via Ollama (local) | Free, 1.8B params, fast, visual Q&A capability |
| Backend tools | Python MCP servers | Standard MCP protocol, extensible, works with agent loop |
| Storage | SPIFFS (12MB) | Flat filesystem for markdown + JSONL sessions |
| Memory pattern | Markdown files | Human-readable, editable, persists across reboots |

---

## 11. File Map

```
_FYP/_Antigravity/
  AGENTS.MD                     -- v1 (superseded, kept for traceability)
  AGENTS V2.MD                  -- THIS FILE (current source of truth)
  components.md                 -- Hardware component list
  Form 2_Proposal (...).md      -- Original proposal (v1, superseded)
  Form 2_Proposal (...).pdf     -- Original proposal PDF
  FYP Report Template (...).pdf -- Report template to follow (both reports)
  2506.22355v3.md               -- Ref: Fung et al. Embodied AI Agents
  2509.19843v1.md               -- Ref: Ziliotto et al. PersONAL benchmark
  robotics-15-00055-v2.md       -- Ref: Lisondra et al. Foundation Models for Robots
  espet/                        -- CODEBASE (Mimiclaw fork)
    main/                       -- All C source code
    spiffs_data/                -- Default SPIFFS files (SOUL.md, USER.md, skills)
    docs/                       -- Architecture docs, TODO, onboarding
    build/                      -- Build output (gitignored)
    sdkconfig                   -- ESP-IDF config
```

---

## 12. Work Breakdown

### hm Responsibilities

**Hardware:**
- Wire INMP441 microphone (I2S: BCLK, WS, DATA pins)
- Wire MAX98357A amplifier + speaker (I2S: BCLK, LRC, DIN pins)
- Validate I2S DMA audio capture + playback on ESP32

**Firmware:**
- I2S mic capture -> PCM buffer -> HTTP POST to STT server
- HTTP GET WAV from TTS server -> I2S playback pipeline
- voice_transcribe tool in tool_registry.c
- speak tool in tool_registry.c
- Proactive trigger logic (heartbeat -> morning briefing -> TTS)

**Backend (Python MCP servers):**
- faster-whisper REST server (STT endpoint)
- Piper TTS REST server (TTS endpoint)
- Gmail MCP tool server
- Google Calendar MCP tool server
- Reminder / notification scheduler

**Documentation:**
- Full FYP Report (Ch1 to Ch4 + testing + final)
- Research domain: Ambient intelligence + Voice HCI + Proactive agents

---

### jeff Responsibilities

**Hardware:**
- Wire OV2640 camera module (SCCB/I2C config + parallel data pins)
- Validate SSD1306 OLED (already partially done via tool_oled.c -- extend)
- JPEG frame capture + PSRAM buffering on ESP32

**Firmware:**
- OV2640 init + frame capture firmware
- JPEG compress + HTTP POST to VLM server
- OLED multi-line text rendering for longer AI responses
- camera_capture tool in tool_registry.c
- vision_describe and vision_ask tools in tool_registry.c
- Button-triggered capture flow (GPIO48)

**Backend (Python MCP servers):**
- Ollama + Moondream2 setup + REST endpoint wrapper
- Web search MCP tool (SearXNG or local Tavily)
- OCR MCP tool (Tesseract or local vision model)
- Note capture MCP tool (save timestamped notes to file)
- Pomodoro / focus timer tool

**Documentation:**
- Full FYP Report (Ch1 to Ch4 + testing + final)
- Research domain: Computer vision + Multimodal AI + Contextual task assistance

---

### Shared Responsibilities (Both)

- Wire shared components: button GPIO48, RGB LED
- Maintain Mimiclaw agent loop core C code
- SPIFFS memory files (SOUL.md tuning, USER.md, MEMORY.md)
- Telegram bot (current working channel, keep as fallback/debug)
- MCP server infrastructure (shared Python base)
- Update AGENTS V2.MD as project evolves

---

## 13. Agent Instructions

When working on this project:

1. **TWO separate FYPs exist** -- hm owns voice/proactive module, jeff owns vision/contextual module. Never conflate them.
2. **Codebase is espet/** -- pure C, ESP-IDF v5.5+, FreeRTOS. No Python/Node on device.
3. **Config is in mimi_config.h** -- all constants. Secrets in mimi_secrets.h (gitignored).
4. **Agent loop is the heart** -- main/agent/agent_loop.c implements ReAct pattern. Both modules extend this.
5. **New tools go in main/tools/tool_registry.c** -- follow existing registration pattern.
6. **All backend is local** -- LM Studio at http://HOST_IP:1234/v1, Whisper/Piper/Moondream also on same server.
7. **Memory files are markdown on SPIFFS** -- SOUL.md, USER.md, MEMORY.md.
8. **Telegram is still active** -- current working input channel, keep as fallback during development.
9. **FYP report template is mandatory** -- FYP Report Template (updated 2.7.25).pdf. Both reports must follow exactly.
10. **Time is critical** -- Chapters 1-3 already overdue. Implement and document in parallel. Do not wait for full system before writing.
11. **AGENTS V2.MD is current source of truth** -- AGENTS.MD v1 kept only for traceability.
