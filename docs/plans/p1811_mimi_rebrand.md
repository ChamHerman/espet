# Implementation Plan: ESPET Project Core Rebranding, Secret Cleanup & Enterprise README Rewrite

## Goal Description
Refactor the project to establish **ESPET** (pronounced **"Expert"** or **"X-Pet"**) as the definitive core project identity across the firmware, backend, build systems, and documentation.

Based on user feedback:
1. **Remove Obsolete Secrets & Compile-Time Config**:
   All configuration (LLM models, API keys, Telegram tokens, proxy, tools) has been migrated to the companion backend (`espet-backend`). Therefore, `main/mimi_secrets.h` and `main/mimi_secrets.h.example` are dead legacy code and will be completely removed. The firmware config is streamlined to retain only essential hardware/system constants (`espet_config.h`), stripping out the unused `MIMI_SECRET_*` boilerplate.
2. **Rewrite `README.md` (No Emojis)**:
   Completely rewrite `README.md` following the design, structure, and depth of `C:\repository\harveyherman-webapp-java\readme.md`, strictly omitting all emojis. It will feature centered hero branding, typing SVG, technology badges, architectural pillars, core modules, system tree, quick start guide, pinout table, team details, and upstream MimiClaw credits.
3. **Core Renaming**:
   - Root CMake project: `project(mimiclaw)` -> `project(espet)` (produces `build/espet.bin`).
   - Entry point: `main/mimi.c` -> `main/espet.c`.
   - Types and identifiers: `mimi_msg_t` -> `espet_msg_t`, `mimi_tool_t` -> `espet_tool_t`, `MIMI_*` -> `ESPET_*`.
4. **Active Wake Speech Intact**:
   `"hey mimi"` and `"mimi"` remain fully active in the voice engine alongside `"espet"`, `"expert"`, and `"x-pet"`.
5. **Attribution & Credits**:
   MimiClaw (and OpenClaw) is credited as the foundational open-source project from which ESPET was forked and evolved.

---

## User Review Required

> [!IMPORTANT]
> **Complete Removal of `mimi_secrets.h`**:
> `main/mimi_secrets.h` and `main/mimi_secrets.h.example` will be permanently deleted.
> All code referencing compile-time secrets in `main/` (`wifi_manager.c`, `telegram_bot.c`, `llm_proxy.c`, `serial_cli.c`, `wifi_onboard.c`) will be cleaned up so configuration cleanly resolves from runtime NVS and backend sync.

> [!IMPORTANT]
> **Firmware Binary Output**:
> In root `CMakeLists.txt`, `project(espet)` replaces `project(mimiclaw)`.
> The output binary is `build/espet.bin` (and `espet.elf`). Deployment scripts (`deploy.sh`, `validate.sh`, `release.yml`) are updated to target `espet.bin`.

> [!NOTE]
> **Strict No-Emoji Policy for README**:
> The root `README.md` will strictly follow the layout of `harveyherman-webapp-java/readme.md` without any emoji characters.

---

## Open Questions

None. User instructions are clear and fully incorporated into this plan.

---

## Proposed Changes

```
espet/
├── CMakeLists.txt                         [MODIFY] project(espet)
├── sdkconfig.defaults                     [MODIFY] CONFIG_LWIP_LOCAL_HOSTNAME="espet"
├── sdkconfig.defaults.esp32s3             [MODIFY] Header comments
├── sdkconfig                              [MODIFY] CONFIG_LWIP_LOCAL_HOSTNAME="espet"
├── .gitignore                             [MODIFY] Clean up mimi_secrets references
├── README.md                              [MODIFY] Complete rewrite matching harveyherman-webapp-java (no emojis)
│
├── main/
│   ├── CMakeLists.txt                     [MODIFY] "espet.c" in SRCS
│   ├── espet.c                            [NEW] Renamed from mimi.c; TAG="espet", ESPET_* macros
│   ├── mimi.c                             [DELETE] Renamed to espet.c
│   ├── espet_config.h                     [NEW] Streamlined hardware/system config, no secret boilerplate
│   ├── mimi_config.h                      [DELETE / SHIM] Compatibility redirect to espet_config.h
│   ├── mimi_secrets.h                     [DELETE] Removed obsolete compile-time secrets file
│   ├── mimi_secrets.h.example             [DELETE] Removed obsolete compile-time secrets template
│   ├── bus/
│   │   ├── message_bus.h                  [MODIFY] espet_msg_t, ESPET_CHAN_* macros
│   │   └── message_bus.c                  [MODIFY] espet_msg_t implementation
│   ├── agent/
│   │   ├── agent_loop.c                   [MODIFY] ESPET_* macros, espet_msg_t
│   │   └── context_builder.c              [MODIFY] ESPET_* macros
│   ├── tools/
│   │   ├── tool_registry.h                [MODIFY] espet_tool_t
│   │   ├── tool_registry.c                [MODIFY] espet_tool_t, ESPET_* macros
│   │   ├── tool_audio.h/.c                [MODIFY] espet_tool_t, ESPET_* pin macros
│   │   └── gpio_policy.h/.c               [MODIFY] ESPET_GPIO_* macros
│   ├── channels/
│   │   ├── telegram/telegram_bot.c        [MODIFY] ESPET_* macros, boundary string, remove secret fallback
│   │   ├── telegram/telegram_bot_photo.c  [MODIFY] boundary string
│   │   ├── feishu/feishu_bot.c            [MODIFY] ESPET_* macros, espet_msg_t, remove secret fallback
│   │   └── cloud_link/cloud_link.c        [MODIFY] ESPET_* macros, espet_msg_t
│   ├── cli/serial_cli.c                   [MODIFY] ESPET_* macros, remove secret print logic
│   ├── cron/cron_service.c                [MODIFY] ESPET_* macros, espet_msg_t
│   ├── gateway/ws_server.c                [MODIFY] ESPET_* macros, espet_msg_t
│   ├── heartbeat/heartbeat.c              [MODIFY] ESPET_* macros, espet_msg_t
│   ├── llm/llm_proxy.c/.h                 [MODIFY] ESPET_* macros, remove secret fallback
│   ├── memory/memory_store.c              [MODIFY] ESPET_* macros
│   ├── memory/session_mgr.c               [MODIFY] ESPET_* macros
│   ├── sync/config_sync.c/.h              [MODIFY] ESPET_* macros
│   ├── voice/voice_client.c               [MODIFY] ESPET_* macros, retain wake words ("mimi", "espet", etc.)
│   └── wifi/wifi_manager.c                [MODIFY] ESPET_* macros, remove secret fallback
│
├── espet-backend/
│   ├── config.py                          [MODIFY] ESPET_CLOUD_LINK_ENABLE, voice_wake_words
│   ├── config_store.py                    [MODIFY] ESPET_CRON_MAX_JOBS comment
│   ├── .env.example                       [MODIFY] ESPET_CLOUD_LINK_ENABLE
│   ├── src/api.ts                         [MODIFY] Comments & references
│   ├── data/config*.json (12 files)       [MODIFY] "I am ESPET, a personal AI assistant..."
│   └── README.md                          [MODIFY] Documentation and credits
│
├── skills/
│   └── deploy/
│       ├── SKILL.md                       [MODIFY] Reference ESPET, espet.bin
│       ├── scripts/deploy.sh              [MODIFY] Reference espet.bin, remove mimi_secrets check
│       └── scripts/validate.sh            [MODIFY] Reference espet.bin, remove mimi_secrets check
│
├── .github/workflows/
│   └── release.yml                        [MODIFY] Release binaries: espet.bin, espet-full-*.bin
│
└── Documentation & Guidelines
    ├── README_CN.md, README_JA.md         [MODIFY] ESPET identity & MimiClaw credits (no emojis)
    ├── AGENTS.md                          [MODIFY] ESPET identity, pronunciation, conventions
    ├── .agent/AGENTS.md, AGENTS V2.MD     [MODIFY] ESPET identity, pronunciation, conventions
    └── .agent/SYSTEM_OVERVIEW.md           [MODIFY] Full overview update + recent changes log
```

---

### Component 1: Build System & Secret Deletion

#### [MODIFY] CMakeLists.txt (Root)
```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espet)

# Pre-flash a valid SPIFFS image so first boot does not need runtime formatting.
spiffs_create_partition_image(spiffs spiffs_data FLASH_IN_PROJECT)
```

#### [DELETE] main/mimi_secrets.h & main/mimi_secrets.h.example
Permanently remove both files.

#### [MODIFY] sdkconfig.defaults & sdkconfig
```ini
CONFIG_LWIP_LOCAL_HOSTNAME="espet"
```

#### [MODIFY] .gitignore
Clean up obsolete `mimi_secrets.h` entries.

---

### Component 2: Streamlined Firmware Configuration (`espet_config.h`)

#### [NEW] main/espet_config.h
Contains only hardware pin assignments, FreeRTOS stacks/priorities, SPIFFS layout, and backend connection details. All compile-time secret overrides (`MIMI_SECRET_*`) are eliminated:
```c
#pragma once

/* ESPET Global Hardware and System Configuration */

#ifndef HOST_IP
#define HOST_IP "127.0.0.1"
#endif
#ifndef BACKEND_IP
#define BACKEND_IP "127.0.0.1"
#endif

#define ESPET_BACKEND_URL             "http://" BACKEND_IP ":8000"
#define ESPET_BACKEND_API_KEY         "espet_secret_device_key_2026"
#define ESPET_CLOUD_LINK_ENABLE       1

/* WiFi & Networking */
#define ESPET_WIFI_MAX_RETRY          5
#define ESPET_WIFI_RETRY_BASE_MS      1000
#define ESPET_WIFI_RETRY_MAX_MS       15000

/* Telegram & Feishu (fallback when Cloud Link disabled) */
#define ESPET_TG_POLL_TIMEOUT_S       5
#define ESPET_TG_MAX_MSG_LEN          4096
#define ESPET_TG_POLL_STACK           (12 * 1024)
#define ESPET_TG_POLL_PRIO            5
#define ESPET_TG_POLL_CORE            0
#define ESPET_TG_CARD_SHOW_MS         3000
#define ESPET_TG_CARD_BODY_SCALE      3

#define ESPET_FEISHU_MAX_MSG_LEN      4096
#define ESPET_FEISHU_POLL_STACK       (12 * 1024)
#define ESPET_FEISHU_POLL_PRIO        5
#define ESPET_FEISHU_POLL_CORE        0
#define ESPET_FEISHU_WEBHOOK_PORT     18790
#define ESPET_FEISHU_WEBHOOK_PATH     "/feishu/events"
#define ESPET_FEISHU_WEBHOOK_MAX_BODY (16 * 1024)

/* Agent Loop (Core 1) */
#define ESPET_AGENT_STACK             (24 * 1024)
#define ESPET_AGENT_PRIO              6
#define ESPET_AGENT_CORE              1
#define ESPET_AGENT_MAX_HISTORY       20
#define ESPET_AGENT_MAX_TOOL_ITER     10
#define ESPET_MAX_TOOL_CALLS          4
#define ESPET_AGENT_SEND_WORKING_STATUS 1

/* Timezone */
#define ESPET_TIMEZONE                "PST8PDT,M3.2.0,M11.1.0"

/* LLM Proxy */
#define ESPET_LLM_DEFAULT_MODEL       "prism-ml/bonsai-27b"
#define ESPET_LLM_PROVIDER_DEFAULT    "openai"
#define ESPET_LLM_MAX_TOKENS          4096
#define ESPET_LLM_API_URL             "https://api.anthropic.com/v1/messages"
#define ESPET_OPENAI_API_URL          "https://apihub.agnes-ai.com/v1/chat/completions"
#define ESPET_LLM_API_VERSION         "2023-06-01"
#define ESPET_LLM_STREAM_BUF_SIZE     (32 * 1024)
#define ESPET_LLM_LOG_VERBOSE_PAYLOAD 0
#define ESPET_LLM_LOG_PREVIEW_BYTES   160

/* Message Bus */
#define ESPET_BUS_QUEUE_LEN           16
#define ESPET_OUTBOUND_STACK          (12 * 1024)
#define ESPET_OUTBOUND_PRIO           5
#define ESPET_OUTBOUND_CORE           0

/* Memory / SPIFFS Layout */
#define ESPET_SPIFFS_BASE             "/spiffs"
#define ESPET_SPIFFS_CONFIG_DIR       ESPET_SPIFFS_BASE "/config"
#define ESPET_SPIFFS_MEMORY_DIR       ESPET_SPIFFS_BASE "/memory"
#define ESPET_SPIFFS_SESSION_DIR      ESPET_SPIFFS_BASE "/sessions"
#define ESPET_MEMORY_FILE             ESPET_SPIFFS_MEMORY_DIR "/MEMORY.md"
#define ESPET_SOUL_FILE               ESPET_SPIFFS_CONFIG_DIR "/SOUL.md"
#define ESPET_USER_FILE               ESPET_SPIFFS_CONFIG_DIR "/USER.md"
#define ESPET_CONTEXT_BUF_SIZE        (16 * 1024)
#define ESPET_SESSION_MAX_MSGS        20

/* Cron / Heartbeat */
#define ESPET_CRON_FILE               ESPET_SPIFFS_BASE "/cron.json"
#define ESPET_CRON_MAX_JOBS           16
#define ESPET_CRON_CHECK_INTERVAL_MS  (60 * 1000)
#define ESPET_HEARTBEAT_FILE          ESPET_SPIFFS_BASE "/HEARTBEAT.md"
#define ESPET_HEARTBEAT_INTERVAL_MS   (30 * 60 * 1000)
#define ESPET_MIN_VALID_EPOCH         1700000000LL
#define ESPET_CRON_STALE_THRESHOLD_S  900

/* Config Sync */
#define ESPET_SYNC_STATE_FILE         ESPET_SPIFFS_CONFIG_DIR "/sync_state.json"
#define ESPET_CONFIG_SYNC_ENABLED     1
#define ESPET_CONFIG_SYNC_STACK       (12 * 1024)
#define ESPET_CONFIG_SYNC_PRIO        4
#define ESPET_CONFIG_SYNC_CORE        0
#define ESPET_CONFIG_SYNC_INTERVAL_S  300

/* Hardware Pinout */
#define ESPET_GPIO_CONFIG_SECTION     1
#define ESPET_GPIO_OLED_SCL           2
#define ESPET_GPIO_OLED_SDA           1
#define ESPET_GPIO_BTN_CAM            41
#define ESPET_GPIO_BTN_WHITE          -1
#define ESPET_GPIO_LED_WHITE          42
#define ESPET_GPIO_I2S_BCLK           38
#define ESPET_GPIO_I2S_WS             39
#define ESPET_GPIO_I2S_DOUT           21
#define ESPET_GPIO_I2S_DIN            40

/* Skills */
#define ESPET_SKILLS_PREFIX           ESPET_SPIFFS_BASE "/skills/"

/* WebSocket Gateway */
#define ESPET_WS_PORT                 18789
#define ESPET_WS_MAX_CLIENTS          4

/* Camera Streaming */
#define ESPET_CAM_STREAM_FPS_DEFAULT  8
#define ESPET_CAM_STREAM_FPS_MAX      15
#define ESPET_CAM_STREAM_STACK        4096
#define ESPET_CAM_STREAM_CORE         0

/* Serial CLI */
#define ESPET_CLI_STACK               (4 * 1024)
#define ESPET_CLI_PRIO                3
#define ESPET_CLI_CORE                0

/* NVS Namespaces & Keys */
#define ESPET_NVS_WIFI                "wifi_config"
#define ESPET_NVS_TG                  "tg_config"
#define ESPET_NVS_FEISHU              "feishu_config"
#define ESPET_NVS_LLM                 "llm_config"
#define ESPET_NVS_PROXY               "proxy_config"
#define ESPET_NVS_SEARCH              "search_config"
#define ESPET_NVS_KEY_SSID            "ssid"
#define ESPET_NVS_KEY_PASS            "password"
#define ESPET_NVS_KEY_TG_TOKEN        "bot_token"
#define ESPET_NVS_KEY_FEISHU_APP_ID   "app_id"
#define ESPET_NVS_KEY_FEISHU_APP_SECRET "app_secret"
#define ESPET_NVS_KEY_API_KEY         "api_key"
#define ESPET_NVS_KEY_TAVILY_KEY      "tavily_key"
#define ESPET_NVS_KEY_MODEL           "model"
#define ESPET_NVS_KEY_PROVIDER        "provider"
#define ESPET_NVS_KEY_PROXY_HOST      "host"
#define ESPET_NVS_KEY_PROXY_PORT      "port"
#define ESPET_NVS_KEY_PROXY_TYPE      "proxy_type"

/* WiFi Onboarding */
#define ESPET_ONBOARD_AP_PREFIX       "ESPET-"
#define ESPET_ONBOARD_AP_PASS         ""
#define ESPET_ONBOARD_HTTP_PORT       80
#define ESPET_ONBOARD_DNS_STACK       (4 * 1024)
#define ESPET_ONBOARD_MAX_SCAN        20
#define ESPET_BLE_PROV_PREFIX         "PROV_ESPET_"
#define ESPET_BLE_PROV_RESTART_DELAY_MS 2000
```

#### [MODIFY] main/bus/message_bus.h & message_bus.c
Define `espet_msg_t` and `ESPET_CHAN_*`:
```c
#define ESPET_CHAN_TELEGRAM   "telegram"
#define ESPET_CHAN_FEISHU     "feishu"
#define ESPET_CHAN_WEBSOCKET  "websocket"
#define ESPET_CHAN_CLI        "cli"
#define ESPET_CHAN_SYSTEM     "system"
#define ESPET_CHAN_CLOUD      "cloud"

typedef struct {
    char channel[16];
    char chat_id[96];
    char *content;
} espet_msg_t;

typedef espet_msg_t mimi_msg_t; // backward compatibility
```

#### [MODIFY] main/tools/tool_registry.h & tool_registry.c
Define `espet_tool_t`:
```c
typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;
    esp_err_t (*execute)(const char *input_json, char *output, size_t output_size);
    bool hidden;
} espet_tool_t;

typedef espet_tool_t mimi_tool_t; // backward compatibility
```

---

### Component 3: Firmware Implementation Cleanup

#### [NEW] main/espet.c (Renamed from main/mimi.c)
- `static const char *TAG = "espet";`
- Include `"espet_config.h"`.
- Use `espet_msg_t`, `ESPET_*` macros.
- Remove references to `mimi.c` and compile-time secrets.

#### [MODIFY] main/CMakeLists.txt
- Change `"mimi.c"` to `"espet.c"`.

#### [MODIFY] All C Files in `main/`
- Strip any remaining `MIMI_SECRET_*` logic (fall back gracefully to NVS or empty string when no NVS credential is set).
- Replace `#include "mimi_config.h"` with `#include "espet_config.h"`.
- Replace `MIMI_*` macros with `ESPET_*`.
- Replace `mimi_msg_t` with `espet_msg_t`.
- In `main/channels/telegram/telegram_bot.c` & `telegram_bot_photo.c`:
  - Change multipart form boundary from `----WebKitFormBoundaryesp32s3mimi` to `----WebKitFormBoundaryesp32s3espet`.
- In `main/voice/voice_client.c`:
  - Retain wake speech recognition for `"hey mimi"` and `"mimi"` alongside `"espet"`, `"expert"`, and `"x-pet"`.

---

### Component 4: Complete `README.md` Rewrite (No Emojis)

The new `README.md` will strictly replicate the structure and layout of `C:\repository\harveyherman-webapp-java\readme.md`, with zero emojis:

1. **Header Block (`<div align="center">`)**:
   - Banner image / logo.
   - Dynamic Typing SVG banner (`readme-typing-svg`):
     - Line 1: `ESPET: Embodied Personal AI Assistant`
     - Line 2: `ESP32-S3 Dual-Core FreeRTOS Architecture`
     - Line 3: `Pronounced "Expert" or "X-Pet"`
     - Line 4: `Hands-Free Voice Wake ("Hey Mimi" / "Hey ESPET")`
     - Line 5: `Real-Time Vision, Multimodal Tools & FastAPI Backend`
     - Line 6: `Persistent Flash Memory & Hierarchical Context`
   - Bold summary paragraph:
     `An embodied personal AI assistant architecture engineered on ESP32-S3 and FastAPI, delivering hands-free voice interaction, real-time vision, multimodal tools, and persistent memory.`
   - Shields/badges: License MIT, ESP-IDF v5.5.x, FreeRTOS, ESP32-S3 N16R8, Python 3.11+, FastAPI, React 19.
2. **Executive Summary**:
   - Comprehensive overview of ESPET (phonetically pronounced "Expert" / "X-Pet").
   - Four fundamental architectural pillars:
     - Embodied Physical Perception & Sensing
     - Dual-Core Xtensa FreeRTOS Agent Loop
     - Asynchronous FastAPI Companion & MCP Gateway
     - Persistent Hierarchical Memory & Personality
3. **Technology Stack**:
   - Categorized badge displays for Presentation Layer, Companion Backend, Embedded Firmware/RTOS, and Microcontroller/Hardware.
4. **Core System Modules & Features**:
   - 1. On-Device ReAct Agent Loop & Context Builder
   - 2. Hands-Free Voice & Wake Speech Subsystem (explicitly documenting both "Hey Mimi" and "Hey ESPET" / "Hey Expert")
   - 3. Vision & Multimodal Perception Module (OV2640 DVP)
   - 4. Hardware Actuation & Peripheral Control (SSD1306 OLED, I2S Audio, White LED, Tactile Switches)
   - 5. Companion Backend & Model Context Protocol (MCP) Host
   - 6. Modern Web Dashboard & 3D Interactive Control Center
5. **System Architecture**:
   - High-level data flow diagram.
   - Comprehensive directory tree layout.
6. **Quick Start: Full Setup Guide**:
   - Quick Access URLs table.
   - Step 1: Hardware & Software Prerequisites (table).
   - Step 2: Backend Setup & Launch (virtualenv, dependencies, uvicorn).
   - Step 3: Firmware Compilation & Flashing (`project(espet)`, `build/espet.bin`).
   - Step 4: BLE Wi-Fi Onboarding & Discovery.
7. **Hardware Pinout Reference**:
   - Exact pin mapping table for all peripherals.
8. **Project Team & Acknowledgments**:
   - Team table (Cham Herman, Jeff Wong).
   - Acknowledgments & Credits section crediting MimiClaw and OpenClaw as the foundational open-source project.
9. **License**:
   - MIT License statement.

---

### Component 5: Backend & Deployment Synchronization

#### [MODIFY] espet-backend/config.py
Ensure `voice_wake_words` supports all variations:
```python
voice_wake_words: str = "espet,hey espet,hi espet,ok espet,expert,hey expert,hi expert,x-pet,hey x-pet,xpet,hey xpet,mimi,hey mimi,hi mimi,ok mimi,米米,咪咪,小米"
```

#### [MODIFY] espet-backend/data/config*.json (12 JSON config files)
Update the seeded persona:
```json
"soul_md": "I am ESPET (pronounced 'Expert' / 'X-Pet'), a personal AI assistant running on an ESP32-S3 microcontroller...\n"
```

#### [MODIFY] skills/deploy/scripts/deploy.sh & validate.sh
- Update to target `build/espet.bin`.
- Remove checks for `main/mimi_secrets.h`.

#### [MODIFY] .github/workflows/release.yml
- Release artifacts: `espet.bin` and `espet-full-${VERSION}.bin`.

#### [MODIFY] .agent/SYSTEM_OVERVIEW.md
- Document the removal of `mimi_secrets.h`, migration to `espet_config.h`, renaming to `espet.c` and `espet.bin`.
- Add entry to Recent Changes log.

---

## Verification Plan

### Automated Tests & Compiles
1. **Clean ESP-IDF Firmware Build**:
   ```bash
   python "C:/esp/v5.5.4/esp-idf/tools/idf.py" set-target esp32s3
   python "C:/esp/v5.5.4/esp-idf/tools/idf.py" fullclean
   python "C:/esp/v5.5.4/esp-idf/tools/idf.py" build
   ```
   **Verification Criteria**:
   - Build completes with return code 0.
   - Produces `build/espet.bin` and `build/espet.elf`.
   - Compiles cleanly with zero references to `mimi_secrets.h`.

2. **Backend Python Compilation**:
   ```bash
   python -m py_compile espet-backend/config.py espet-backend/config_store.py espet-backend/api/voice_router.py
   ```

3. **Wake Word Verification**:
   Execute regex test to confirm both `"hey mimi"` and `"hey espet"` / `"hey expert"` trigger wake recognition.

4. **Emoji Lint on `README.md`**:
   Verify `README.md` contains 0 emoji characters using a regex check (`[\U0001F000-\U0001FAFF\U00002600-\U000027BF]`).

5. **Codebase Grep Check**:
   Confirm no unauthorized `mimi` or `mimiclaw` tokens remain outside of credits and wake word speech patterns.

### Manual Verification
1. Review `README.md` formatting in preview.
2. Confirm Serial CLI banner and prompt: `espet> `.
