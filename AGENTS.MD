# Repository Guidelines

## Project Overview

MimiClaw (ESPET) is an embodied personal AI assistant architecture divided across two synchronized components:

1. **ESP32-S3 Firmware (`main/`)**: Pure C firmware running under FreeRTOS on ESP-IDF (v5.5+). It operates directly on an ESP32-S3 microcontroller (16 MB Flash, 8 MB Octal PSRAM) to manage local hardware peripherals (OLED display, I2S microphone/speaker, OV2640 camera, push buttons, status LED), local SPIFFS markdown memory, multi-channel communication (Telegram bot, Feishu/Lark, WebSocket, UART serial console), and an on-device ReAct agent loop (Core 1, up to 10 turns) communicating with Anthropic or OpenAI-compatible LLM endpoints.
2. **FastAPI Companion Backend (`espet-backend/`)**: An asynchronous Python 3.11+ service operating on the local network (default port 8000). It serves as a Model Context Protocol (MCP) host, managing connections to external services (Google Docs, Gmail, Calendar, Drive), storing encrypted OAuth tokens in SQLite, handling parameter normalization for small LLMs, and providing bidirectional configuration synchronization.

**Core Architectural Invariant**: The ESP32 never runs Python, node runtimes, or complex OAuth handshakes; the Python backend never directly touches hardware GPIO, I2C, or I2S buses.

---

## Architecture & Data Flow

### 1. End-to-End Message & Execution Flow
```
[Inbound Channels]
(Telegram / Feishu / WebSocket / Serial CLI / Cron / Heartbeat)
       │
       ▼
[FreeRTOS Inbound Queue: s_inbound_queue (Core 0)]
       │
       ▼
[Agent Loop Task: agent_loop_task (Core 1)]
  ├─ 1. Build context via context_builder.c (SOUL.md, USER.md, MEMORY.md, skills, pins)
  ├─ 2. Fetch session history via session_mgr.c (/spiffs/sessions/<chat_id>.jsonl)
  ├─ 3. Call LLM via llm_proxy.c (Anthropic Messages or OpenAI Chat Completions)
  ├─ 4. If Tool Call: Dispatch via tool_registry.c
  │      ├─ Local C Tool  ──> GPIO, OLED, I2S Audio, Camera, Filesystem, Cron
  │      └─ Remote MCP    ──> HTTP POST /api/mcp/execute (espet-backend:8000)
  └─ 5. Push final answer to s_outbound_queue
       │
       ▼
[Outbound Dispatcher: outbound_dispatch_task (Core 0)]
       │
       ▼
[Channel Delivery: Telegram / Feishu / WebSocket client]
```

### 2. Dual-Core FreeRTOS Task Layout
The ESP32-S3 dual Xtensa LX7 cores strictly separate networking from agent compute:

| Task Name | Function | Priority | Stack | Core | Primary Responsibility |
|---|---|---|---|---|---|
| `agent_loop` | `agent_loop_task` | 6 | 24 KB | **Core 1** | Prompt assembly, LLM API calls, ReAct tool execution loop |
| `audio_cap` | `capture_task` | 7 | 4 KB | **Core 1** | Real-time INMP441 I2S DMA microphone capture (priority > agent loop) |
| `outbound` | `outbound_dispatch_task` | 5 | 12 KB | **Core 0** | Pops `s_outbound_queue`, transmits messages over network |
| `tg_poll` | `telegram_poll_task` | 5 | 12 KB | **Core 0** | Long-polling client hitting Telegram Bot API `/getUpdates` |
| `feishu_ws` | `feishu_ws_task` | 5 | 12 KB | **Core 0** | WebSocket/webhook client receiving Feishu event subscriptions |
| `cfg_sync` | `sync_task_main` | 4 | 12 KB | **Core 0** | Periodic background pull from backend (`GET /api/config`) |
| `serial_cli`| `serial_cli_task` | 3 | 4 KB | **Core 0** | Interactive console REPL via UART COM port |
| `button_task`| `button_task` | 10 | 16 KB | Unpinned | Debounces push buttons and processes clicks/holds |
| `cron` | `cron_task_main` | 5 | 4 KB | Unpinned | Background 60-second timer checking job triggers |
| `oled_anim` | `oled_anim_task` | 5 | 4 KB | Unpinned | Autonomous idle face animation on SSD1306 display |

### 3. Queue & Memory Architecture
- **Inter-Task Queues**: `s_inbound_queue` (capacity 16) and `s_outbound_queue` (capacity 16) pass pointers to dynamically allocated `mimi_msg_t` structures. The pushing task allocates `content` on the heap; the popping task takes ownership and must `free()` it.
- **DRAM vs PSRAM Partitioning**: Internal SRAM (~40 KB usable) is reserved for FreeRTOS task stacks, queues, and DMA buffers. All large buffers (≥32 KB stream buffers, TLS records, JSON AST trees, LLM prompts) must reside in external 8 MB Octal PSRAM via `heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM)`.

---

## Key Directories

```
.
├── main/                   # ESP-IDF C firmware source code
│   ├── agent/              # ReAct loop (agent_loop.c) and prompt builder (context_builder.c)
│   ├── bus/                # Thread-safe FreeRTOS message bus (message_bus.c)
│   ├── channels/           # Inbound/outbound adapters: telegram/, feishu/
│   ├── cli/                # Interactive serial console REPL (serial_cli.c)
│   ├── cron/               # Persistent scheduled job manager (cron_service.c)
│   ├── gateway/            # Embedded WebSocket server on port 18789 (ws_server.c)
│   ├── heartbeat/          # Autonomous task runner checking HEARTBEAT.md (heartbeat.c)
│   ├── llm/                # Unified Anthropic & OpenAI API client (llm_proxy.c)
│   ├── memory/             # Markdown memory store and JSONL chat session manager
│   ├── onboard/            # SoftAP captive portal for initial provisioning (wifi_onboard.c)
│   ├── ota/                # Over-The-Air firmware updater (ota_manager.c)
│   ├── skills/             # Runtime SPIFFS skill parser (skill_loader.c)
│   ├── sync/               # Backend configuration synchronization client (config_sync.c)
│   ├── tools/              # Hardware/OS C tools (gpio, oled, audio, camera, files, web)
│   └── wifi/               # Station & AP Wi-Fi manager (wifi_manager.c)
├── spiffs_data/            # Source tree bundled into the 11.8 MB SPIFFS flash partition
│   ├── config/             # SOUL.md (personality) and USER.md (user profile)
│   ├── memory/             # MEMORY.md (long-term memory)
│   └── skills/             # Modular markdown skills (gpio-control.md, weather.md, etc.)
├── espet-backend/          # Companion FastAPI Python service (LAN port 8000)
│   ├── api/                # Endpoints: /api/mcp/execute, /api/tools, /api/config, /auth
│   ├── auth/               # Device API key verification, OAuth manager, SQLite token store
│   ├── config/             # Remote MCP server registry (mcp_servers.json)
│   ├── mcp/                # Multi-server MCP client and tool registry
│   ├── src/                # Dashboard SPA source (React 19 + @lobehub/ui, TSX)
│   ├── static/             # Built dashboard (static/vendor/app) + 3D avatar assets
│   └── tools/              # esbuild build scripts (toy-entry.js, build-app.mjs)
├── scripts/                # Platform setup and build helpers (setup_idf_*.sh, build_*.sh)
├── skills/deploy/scripts/  # Deployment orchestration (deploy.sh) and pre-flight check (validate.sh)
├── docs/                   # Technical documentation, hardware test runbooks, integration guides
└── .agent/                 # Architecture references, pin maps, and SOPs
```

---

## Development Commands

### 1. ESP-IDF Firmware (ESP32-S3)

Always export the ESP-IDF environment before invoking `idf.py`:
```bash
# Linux / macOS
. "$IDF_PATH/export.sh"

# Windows (Command Prompt / PowerShell)
%IDF_PATH%\export.bat
# or: . $env:IDF_PATH\export.ps1
```

```bash
# 1. Target configuration (first time)
idf.py set-target esp32s3

# 2. Standard build
idf.py build

# 3. Clean incremental build artifacts
idf.py clean

# 4. Mandatory full clean (required after editing mimi_secrets.h, partitions.csv, or sdkconfig.defaults)
idf.py fullclean && idf.py build

# 5. Flash and open serial monitor
idf.py -p <PORT> flash monitor
# Exit monitor: Ctrl+]
```

#### Dual USB Port Identification
ESP32-S3 development boards feature two USB-C ports with distinct roles:
- **Native USB (USB Serial/JTAG)**: Used for high-speed flashing (`idf.py flash` at 2,000,000 baud).
  - macOS: `/dev/cu.usbmodem*` | Linux: `/dev/ttyACM*` | Windows: `COMx` (USB JTAG)
- **COM Port (External UART Bridge CP2102/CH340)**: Used for the interactive Serial REPL console (`mimi> ` at 115,200 baud).
  - macOS: `/dev/cu.usbserial*` | Linux: `/dev/ttyUSB*` | Windows: `COMx` (Silicon Labs CP210x)

*Note: The interactive REPL console does not receive input over native USB JTAG; use the COM UART port for console interaction.*

### 2. Python Backend (`espet-backend/`)

```bash
cd espet-backend

# 1. Initialize virtual environment and install dependencies
python3 -m venv venv
source venv/bin/activate       # Windows: venv\Scripts\activate
pip install -r requirements.txt

# 2. Environment configuration
cp .env.example .env           # Update DEVICE_API_KEY, GOOGLE_CLIENT_ID/SECRET as needed

# 3. Launch server with hot reload
uvicorn main:app --reload --host 0.0.0.0 --port 8000
# Or run directly: python main.py
```

### 3. Frontend Build (dashboard SPA + 3D avatar assets)
The dashboard is a **React 19 + antd 6 + `@lobehub/ui`** SPA (source in
`espet-backend/src/`, TypeScript). esbuild bundles it to `static/vendor/app/`
(ESM + code-splitting) and the 3D viewer to `static/vendor/toy.bundle.js`; both
are **checked into git** (firmware CI never runs Node). Rebuild after editing
`src/**` or `tools/toy-entry.js`:
```bash
cd espet-backend
npm install --legacy-peer-deps   # lobe-charts peer-wants @lobehub/ui ^4; 5.x is fine
npm run build:app                # src/ → static/vendor/app/ (stamps cache-bust ?v in index.html)
npm run build:vendor             # tools/toy-entry.js → static/vendor/toy.bundle.js
```
The SPA consumes the existing `/api/*`, `/health`, `/auth/*` endpoints — no
backend change is needed to run it. `ui_router.py` still serves `static/index.html`.

### 4. CI Build Verification
GitHub Actions (`.github/workflows/build.yml`) validates compilation on every push using container `espressif/idf:v5.5.2`:
```bash
. "$IDF_PATH/export.sh"
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

---

## Code Conventions & Common Patterns

### C Firmware (`main/`)

1. **Naming Conventions**:
   - Module prefixes: `mimi_` (system, config, bus), `tool_` (hardware/functional tools), `session_` (session history), `wifi_` (networking).
   - Internal static globals: prefixed with `s_` (e.g. `s_inbound_queue`, `s_api_key`).
   - Macros and constants: all caps with `MIMI_` prefix (e.g. `MIMI_AGENT_STACK`, `MIMI_ROUTER_TIMEOUT_MS`).
   - Logging tags: static string matching the module name (e.g. `static const char *TAG = "agent";`).

2. **Memory Allocation Rules**:
   - **Internal DRAM**: Reserved strictly for FreeRTOS task stacks, DMA buffers, hardware queues, and small descriptors. Avoid large local stack arrays.
   - **External PSRAM**: All buffers ≥32 KB, network TLS records, JSON AST trees (`cJSON`), and LLM context strings must be allocated in PSRAM using `heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM)`.
   - **Queue Transfer Ownership**: FreeRTOS queues transfer ownership of dynamic buffers. The producing task allocates; the consuming task frees.

3. **Error Handling**:
   - Always return `esp_err_t` (`ESP_OK`, `ESP_FAIL`, `ESP_ERR_NO_MEM`, `ESP_ERR_TIMEOUT`).
   - Fatal startup errors use `ESP_ERROR_CHECK(err)`.
   - Recoverable runtime errors log via `ESP_LOGE(TAG, "Operation failed: %s", esp_err_to_name(err))` and return gracefully.

4. **Hardware Safety Policy (`main/tools/gpio_policy.c`)**:
   - User-invoked GPIO tools are constrained by `MIMI_GPIO_ALLOWED_CSV`.
   - Pins reserved for Octal Flash/PSRAM (GPIO 33–37), USB JTAG (GPIO 19, 20), and UART console (GPIO 43, 44) are strictly blocked.

5. **Hardware Tool-First Mandate**:
   - If a user prompt requests hardware interaction (LED toggle, OLED render, audio tone, button inspection), the agent loop strictly requires calling `gpio_write`, `oled_print`, or `audio_play_tone` **before** outputting conversational text. Never ask the user for pin numbers.

### Python Backend (`espet-backend/`)

1. **Asynchronous Architecture**:
   - All router handlers, HTTP client calls (`httpx.AsyncClient`), and MCP transports must use `async`/`await`. Never use blocking I/O calls in route handlers.

2. **Dependency Injection & Security**:
   - Protected API routes must inject `auth/device_auth.py`:
     ```python
     @router.post("/api/mcp/execute")
     async def execute_tool(req: MCPExecuteRequest, auth: bool = Depends(verify_device_key)):
         ...
     ```
   - Validates `Authorization: Bearer <DEVICE_API_KEY>` matching `settings.device_api_key`.

3. **Atomic File Persistence (`config_store.py`)**:
   - Config updates write to a temporary file (`config.json.tmp`) before calling `os.replace` under a `threading.Lock()` to prevent file corruption during power cuts or concurrent requests.

4. **MCP Argument Normalization & Recovery (`api/mcp_normalizer.py`)**:
   - Proactively resolves small-LLM hallucinations: converts snake_case to camelCase and normalizes aliases (e.g. `recipient` → `to`).
   - Catches JSON-RPC error responses and attempts regex-based parameter repair before returning an error.

---

## Important Files

### Entry Points
- `main/mimi.c`: Firmware boot sequence, subsystem initialization, button interrupt configuration, Wi-Fi onboarding fallback, and task dispatcher startup.
- `espet-backend/main.py`: FastAPI server startup, CORS setup, router mounting, and MCP lifespan manager.

### Configuration & Schemas
- `main/mimi_config.h`: Central hardware pin assignments, task priorities/stack sizes, memory limits, and default endpoints.
- `main/mimi_secrets.h.example`: Template for compile-time credentials (Wi-Fi, Telegram bot token, API keys, proxy).
- `partitions.csv`: 16 MB flash layout defining `nvs` (24 KB), `otadata` (8 KB), dual app slots (`ota_0` 2 MB, `ota_1` 2 MB), and `spiffs` (11.8 MB).
- `sdkconfig.defaults.esp32s3`: Hardware defaults: 16 MB QIO flash, 240 MHz CPU, 8 MB Octal PSRAM, UART console.
- `espet-backend/.env.example`: Template for backend server host/port, device API key, Google OAuth secrets, and MCP URLs.
- `espet-backend/config/mcp_servers.json`: Manifest for remote Google MCP servers (Gmail, Calendar, Drive, Docs).

### Core Modules
- `main/agent/agent_loop.c`: ReAct execution loop handling multi-turn LLM conversations and tool results.
- `main/tools/tool_registry.c`: Central registry dispatching local C functions and forwarding remote MCP tool calls to the backend.
- `main/sync/config_sync.c`: Background daemon pulling updated persona/profile/cron settings from the backend.
- `espet-backend/api/mcp_router.py`: Generic endpoint (`POST /api/mcp/execute`) executing remote tools with rate limiting and normalizers.
- `espet-backend/auth/token_store.py`: SQLite-backed encrypted OAuth credential storage with automatic token refresh.

---

## Runtime & Tooling Preferences

- **Firmware Toolchain**: ESP-IDF `v5.5.x` (`>=5.5.0,<5.6.0`, tested with v5.5.2). Target chip is strictly `esp32s3`.
- **Target Microcontroller**: `ESP32-S3-WROOM-1-N16R8` (16 MB Flash, 8 MB Octal PSRAM).
- **Backend Runtime**: Python `3.11+` using standard `venv` and `pip`.
- **Node.js / Bun**: Not required for daily development. Node/esbuild is only used optionally to compile 3D frontend assets via `package.json`.
- **CMake Dependency Registration**: When adding a new `.c` file under `main/`, you **must** register it in the `SRCS` list of `main/CMakeLists.txt`. Forgetting this produces unresolved linker errors.

---

## Testing & QA

### Status of Automated Suites
- **Firmware**: No unit test framework (Unity/CMock) is currently wired in CI. CI verifies compilation correctness via `idf.py build`.
- **Backend**: No pytest suite is currently configured in `espet-backend/`. Validation relies on smoke test commands and health checks.

### Firmware Verification Procedures
1. **Interactive Serial CLI Smoke Tests**:
   Connect a terminal to the UART COM port at 115200 baud and execute:
   ```text
   mimi> tool_exec gpio_write '{"pin":42,"state":1}'  # Verify status LED
   mimi> tool_exec audio_play_tone "{}"               # Test speaker (440Hz sine wave)
   mimi> tool_exec audio_record_test "{}"             # Test microphone capture
   mimi> heap_info                                    # Check PSRAM and internal SRAM leaks
   mimi> wifi_status                                  # Check IP and signal
   ```

2. **Physical Button & Telegram Runbook (`docs/telegram_hardware_testing.md`)**:
   - **Green Button (GPIO 41)**: Short click captures an OV2640 snapshot; press and hold performs multimodal push-to-talk (snapshot + INMP441 voice query). *(White Button is unassigned/disabled; GPIO 38/39 are shared I2S audio clocks.)*
   - **Telegram Commands**: Send prompts to verify the ReAct loop:
     - `"Turn on the white LED"` → verifies `gpio_write` on pin 42.
     - `"Display 'Hello' on OLED"` → verifies `oled_print` on GPIO 1 (SDA) / GPIO 2 (SCL).
     - `"Read all GPIO pins"` → verifies `gpio_read_all` (safe pins `41, 42`).

### Backend Verification Procedures
```bash
# 1. Health check
curl http://localhost:8000/health

# 2. Check OAuth status
curl http://localhost:8000/auth/status

# 3. List discovered tools (requires device key)
curl -H "Authorization: Bearer YOUR_DEVICE_API_KEY" http://localhost:8000/api/tools

# 4. Execute tool test
curl -X POST http://localhost:8000/api/mcp/execute \
     -H "Authorization: Bearer YOUR_DEVICE_API_KEY" \
     -H "Content-Type: application/json" \
     -d '{"tool_name":"list_recent_files","arguments":{"limit":5}}'
```

---

## Hardware Pinout Reference

Centrally configured in `main/mimi_config.h` and documented in `.agent/ESPET_PINS.MD`:

| Peripheral | Component | Pin | Protocol / Details |
|---|---|---|---|
| **OLED Clock** | SSD1306 | **GPIO 2** | I2C0 SCL (400 kHz, internal pull-up) |
| **OLED Data** | SSD1306 | **GPIO 1** | I2C0 SDA (400 kHz, internal pull-up) |
| **White Push Button** | Tactile Switch | *Unassigned* | Unassigned / disabled |
| **Green Push Button** | Tactile Switch | **GPIO 41** | Input pull-up; Any-edge interrupt (PTT voice record) |
| **Status LED** | Discrete White LED | **GPIO 42** | Output (1 = HIGH / ON, 0 = LOW / OFF) |
| **Audio I2S BCLK** | INMP441 / MAX98357A | **GPIO 38** | I2S0 Master Bit Clock |
| **Audio I2S WS** | INMP441 / MAX98357A | **GPIO 39** | I2S0 Word Select / Frame Clock |
| **Audio Mic Data In** | INMP441 Microphone | **GPIO 40** | I2S0 Data Input (`DIN`/`SD`) |
| **Audio Speaker Out**| MAX98357A Amplifier | **GPIO 21** | I2S0 Data Output (`DOUT`) |
| **Camera Sensor** | OV2640 DVP | **GPIO 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18** | DVP parallel bus & SCCB |
| **Serial Console** | UART0 (CP2102/CH340)| **GPIO 43 (TX), 44 (RX)** | 115200 baud interactive REPL console |
| **Native USB** | ESP32-S3 USB/JTAG | **GPIO 19 (D-), 20 (D+)** | Native USB flashing and JTAG |

---

## SOP: Extending Capabilities

Refer to `.agent/ADDING_TOOLS_SOP.md` when expanding system capabilities:

1. **Tier 1: Adding a Remote MCP Service (No C code)**:
   - Add service definition to `espet-backend/config/mcp_servers.json` with required OAuth scopes.
   - Add credentials/endpoints to `espet-backend/.env` and `config.py`.
   - Update UI categories in `espet-backend/api/ui_router.py`.
   - Restart the backend. The ESP32 discovers new tools on boot via `GET /api/tools`.

2. **Tier 2: Adding a Custom Backend Tool (Python)**:
   - Implement the async handler in `espet-backend/api/custom_tools.py`.
   - Register the function in `CUSTOM_TOOLS` map.

3. **Tier 3: Adding a Local Hardware C Tool**:
   - Implement `main/tools/tool_<name>.h` and `main/tools/tool_<name>.c` implementing `tool_<name>_execute(const char *input_json, char *output, size_t output_size)`.
   - **Crucial**: Add `"tools/tool_<name>.c"` to `SRCS` in `main/CMakeLists.txt`.
   - Define schema and register handler in `main/tools/tool_registry.c`.
   - Initialize the tool in `main/mimi.c`.

## Session Memory Rule (Mandatory)

Every update in feature/new implementation — regardless of size — MUST auto-update
`.agent/SYSTEM_OVERVIEW.md` in the same change (no separate request needed).

- What to update: any section the change touches (architecture, API surface, file map,
  flows, limitations) plus a one-line entry in the "Recent Changes" log at the bottom
  of that file: `- YYYY-MM-DD — <change summary> (files: ...)`.
- Scope: features, behavior changes, new files/modules, schema/API/config changes.
  Pure refactors that change no behavior are exempt.
- Rule is enforced during every session; verify SYSTEM_OVERVIEW.md diff before finishing.

## Design Token Rule (Mandatory)

`DESIGN.MD` (repo root) is the single source of truth for the frontend's visual
language — monochrome black/white system, dim-blue primary buttons, typography,
glass, motion, and component rules.

- Any change to the frontend UI (`espet-backend/src/`, `static/index.html`,
  themes, components, views) MUST follow the tokens in `DESIGN.MD`.
- Any change that introduces, alters, or retires a token, pattern, or
  component rule MUST update `DESIGN.MD` in the same change — no separate
  request needed. Verify the DESIGN.MD diff alongside the SYSTEM_OVERVIEW.md
  diff before finishing.
