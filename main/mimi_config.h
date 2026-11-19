#pragma once

/* ESPET Global Configuration */

/* Build-time secrets (highest priority, override NVS) */
#if __has_include("mimi_secrets.h")
#include "mimi_secrets.h"
#endif

#ifndef MIMI_SECRET_WIFI_SSID
#define MIMI_SECRET_WIFI_SSID ""
#endif
#ifndef MIMI_SECRET_WIFI_PASS
#define MIMI_SECRET_WIFI_PASS ""
#endif
#ifndef MIMI_SECRET_TG_TOKEN
#define MIMI_SECRET_TG_TOKEN ""
#endif
#ifndef MIMI_SECRET_API_KEY
#define MIMI_SECRET_API_KEY ""
#endif
#ifndef MIMI_SECRET_MODEL
#define MIMI_SECRET_MODEL ""
#endif
#ifndef MIMI_SECRET_MODEL_PROVIDER
#define MIMI_SECRET_MODEL_PROVIDER "openai"
#endif
#ifndef MIMI_SECRET_PROXY_HOST
#define MIMI_SECRET_PROXY_HOST ""
#endif
#ifndef MIMI_SECRET_PROXY_PORT
#define MIMI_SECRET_PROXY_PORT ""
#endif
#ifndef MIMI_SECRET_PROXY_TYPE
#define MIMI_SECRET_PROXY_TYPE ""
#endif
#ifndef MIMI_SECRET_SEARCH_KEY
#define MIMI_SECRET_SEARCH_KEY ""
#endif

/* Feishu Channel Enable Flag: controlled via Kconfig / sdkconfig */
#ifndef MIMI_FEISHU_ENABLE
#if defined(CONFIG_ESPET_ENABLE_FEISHU) && CONFIG_ESPET_ENABLE_FEISHU
#define MIMI_FEISHU_ENABLE 1
#else
#define MIMI_FEISHU_ENABLE 0
#endif
#endif

#if MIMI_FEISHU_ENABLE
#ifndef MIMI_SECRET_FEISHU_APP_ID
#define MIMI_SECRET_FEISHU_APP_ID ""
#endif
#ifndef MIMI_SECRET_FEISHU_APP_SECRET
#define MIMI_SECRET_FEISHU_APP_SECRET ""
#endif
#endif
#ifndef MIMI_SECRET_TAVILY_KEY
#define MIMI_SECRET_TAVILY_KEY ""
#endif

/* WiFi */
#define MIMI_WIFI_MAX_RETRY 5
#define MIMI_WIFI_RETRY_BASE_MS 1000
#define MIMI_WIFI_RETRY_MAX_MS 15000

/* Telegram Bot */
#define MIMI_TG_POLL_TIMEOUT_S 5
#define MIMI_TG_MAX_MSG_LEN 4096
#define MIMI_TG_POLL_STACK (12 * 1024)
#define MIMI_TG_POLL_PRIO 5
#define MIMI_TG_POLL_CORE 0
#define MIMI_TG_CARD_SHOW_MS 3000
#define MIMI_TG_CARD_BODY_SCALE 3

/* Feishu Bot (Archived - enabled only when CONFIG_ESPET_ENABLE_FEISHU=y) */
#if MIMI_FEISHU_ENABLE
#define MIMI_FEISHU_MAX_MSG_LEN 4096
#define MIMI_FEISHU_POLL_STACK (12 * 1024)
#define MIMI_FEISHU_POLL_PRIO 5
#define MIMI_FEISHU_POLL_CORE 0
#define MIMI_FEISHU_WEBHOOK_PORT 18790
#define MIMI_FEISHU_WEBHOOK_PATH "/feishu/events"
#define MIMI_FEISHU_WEBHOOK_MAX_BODY (16 * 1024)
#endif

/* Agent Loop */
#define MIMI_AGENT_STACK (24 * 1024)
#define MIMI_AGENT_PRIO 6
#define MIMI_AGENT_CORE 1
#define MIMI_AGENT_MAX_HISTORY 20
#define MIMI_AGENT_MAX_TOOL_ITER 10
#define MIMI_MAX_TOOL_CALLS 4
#define MIMI_AGENT_SEND_WORKING_STATUS 1

/* Timezone (POSIX TZ format) */
#define MIMI_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"

/* LLM */
#define MIMI_LLM_DEFAULT_MODEL "prism-ml/bonsai-27b"
#define MIMI_LLM_PROVIDER_DEFAULT "openai"
#define MIMI_LLM_MAX_TOKENS 4096
#define MIMI_LLM_API_URL "https://api.anthropic.com/v1/messages"
/* #define MIMI_OPENAI_API_URL "https://api.openai.com/v1/chat/completions" */
#ifndef HOST_IP
#define HOST_IP "127.0.0.1" /* Fallback for LLM */
#endif
#ifndef BACKEND_IP
#define BACKEND_IP "127.0.0.1" /* Fallback for Backend Server */
#endif
#ifdef MIMI_SECRET_OPENAI_URL
#define MIMI_OPENAI_API_URL          MIMI_SECRET_OPENAI_URL
#else
#define MIMI_OPENAI_API_URL          "https://apihub.agnes-ai.com/v1/chat/completions"
#endif
#define MIMI_BACKEND_URL             "http://" BACKEND_IP ":8000"
#define MIMI_BACKEND_API_KEY         "espet_secret_device_key_2026"

/* --- Hybrid mode (Plan C-Hybrid): cloud gateway owns Telegram polling --- */
/* When 1: the device does NOT start telegram_bot polling; it keeps ONE
 * persistent WebSocket to the backend (/ws/device) which relays messages.
 * The backend must have CLOUD_GATEWAY_ENABLED=true + TELEGRAM_BOT_TOKEN set. */
#ifndef MIMI_CLOUD_LINK_ENABLE
#define MIMI_CLOUD_LINK_ENABLE 1
#endif
#define MIMI_LLM_API_VERSION         "2023-06-01"
#define MIMI_LLM_STREAM_BUF_SIZE     (32 * 1024)
#define MIMI_LLM_LOG_VERBOSE_PAYLOAD 0
#define MIMI_LLM_LOG_PREVIEW_BYTES   160

/* Message Bus */
#define MIMI_BUS_QUEUE_LEN 16
#define MIMI_OUTBOUND_STACK (12 * 1024)
#define MIMI_OUTBOUND_PRIO 5
#define MIMI_OUTBOUND_CORE 0

/* Memory / SPIFFS */
#define MIMI_SPIFFS_BASE "/spiffs"
#define MIMI_SPIFFS_CONFIG_DIR MIMI_SPIFFS_BASE "/config"
#define MIMI_SPIFFS_MEMORY_DIR MIMI_SPIFFS_BASE "/memory"
#define MIMI_SPIFFS_SESSION_DIR MIMI_SPIFFS_BASE "/sessions"
#define MIMI_MEMORY_FILE MIMI_SPIFFS_MEMORY_DIR "/MEMORY.md"
#define MIMI_SOUL_FILE MIMI_SPIFFS_CONFIG_DIR "/SOUL.md"
#define MIMI_USER_FILE MIMI_SPIFFS_CONFIG_DIR "/USER.md"
#define MIMI_CONTEXT_BUF_SIZE (16 * 1024)
#define MIMI_SESSION_MAX_MSGS 20

/* Cron / Heartbeat */
#define MIMI_CRON_FILE MIMI_SPIFFS_BASE "/cron.json"
#define MIMI_CRON_MAX_JOBS 16
#define MIMI_CRON_CHECK_INTERVAL_MS (60 * 1000)
#define MIMI_HEARTBEAT_FILE MIMI_SPIFFS_BASE "/HEARTBEAT.md"
#define MIMI_HEARTBEAT_INTERVAL_MS (30 * 60 * 1000)


/* Timekeeping & Cron staleness */
#define MIMI_MIN_VALID_EPOCH 1700000000LL /* Nov 2023 sanity barrier */
#define MIMI_CRON_STALE_THRESHOLD_S 900    /* 15 min expiration window */
/* Config Sync (device pulls USER/SOUL/MEMORY/cron/timezone from backend) */
#define MIMI_SYNC_STATE_FILE MIMI_SPIFFS_CONFIG_DIR "/sync_state.json"
#define MIMI_CONFIG_SYNC_ENABLED 1
#define MIMI_CONFIG_SYNC_STACK (12 * 1024)
#define MIMI_CONFIG_SYNC_PRIO 4
#define MIMI_CONFIG_SYNC_CORE 0
#define MIMI_CONFIG_SYNC_INTERVAL_S 300 /* default; overridden by web device settings */

/* GPIO */
#define MIMI_GPIO_CONFIG_SECTION 1 /* enable GPIO tools */
#define MIMI_GPIO_OLED_SCL 2       /* SSD1306 OLED SCL/SCK (was 21) */
#define MIMI_GPIO_OLED_SDA 1       /* SSD1306 OLED SDA (was 47) */
#define MIMI_GPIO_BTN_CAM 41       /* Camera Push Button (Input Pull-Up): short=snap photo, hold=video */
#define MIMI_GPIO_BTN_WHITE -1     /* (Unassigned) */
#define MIMI_GPIO_LED_WHITE 42     /* White LED Output (was 40) */
#define MIMI_GPIO_I2S_BCLK 38      /* Shared Audio BCLK / SCK (Mic + Amp) (was 41) */
#define MIMI_GPIO_I2S_WS 39        /* Shared Audio WS / LRC (Mic + Amp) (was 42) */
#define MIMI_GPIO_I2S_DOUT 21      /* MAX98357A Amp DIN (was 2) */
#define MIMI_GPIO_I2S_DIN 40       /* INMP441 Mic SD (was 1) */

/* Skills */
#define MIMI_SKILLS_PREFIX MIMI_SPIFFS_BASE "/skills/"

/* WebSocket Gateway */
#define MIMI_WS_PORT 18789
#define MIMI_WS_MAX_CLIENTS 4

/* Camera live streaming (backend-demanded MJPEG relay) */
#define MIMI_CAM_STREAM_FPS_DEFAULT 8
#define MIMI_CAM_STREAM_FPS_MAX 15
#define MIMI_CAM_STREAM_STACK 4096
#define MIMI_CAM_STREAM_CORE 0

/* Serial CLI */
#define MIMI_CLI_STACK (4 * 1024)
#define MIMI_CLI_PRIO 3
#define MIMI_CLI_CORE 0

/* NVS Namespaces */
#define MIMI_NVS_WIFI "wifi_config"
#define MIMI_NVS_TG "tg_config"
#if MIMI_FEISHU_ENABLE
#define MIMI_NVS_FEISHU "feishu_config"
#endif
#define MIMI_NVS_LLM "llm_config"
#define MIMI_NVS_PROXY "proxy_config"
#define MIMI_NVS_SEARCH "search_config"

/* NVS Keys */
#define MIMI_NVS_KEY_SSID "ssid"
#define MIMI_NVS_KEY_PASS "password"
#define MIMI_NVS_KEY_TG_TOKEN "bot_token"
#if MIMI_FEISHU_ENABLE
#define MIMI_NVS_KEY_FEISHU_APP_ID "app_id"
#define MIMI_NVS_KEY_FEISHU_APP_SECRET "app_secret"
#endif
#define MIMI_NVS_KEY_API_KEY "api_key"
#define MIMI_NVS_KEY_TAVILY_KEY "tavily_key"
#define MIMI_NVS_KEY_MODEL "model"
#define MIMI_NVS_KEY_PROVIDER "provider"
#define MIMI_NVS_KEY_PROXY_HOST "host"
#define MIMI_NVS_KEY_PROXY_PORT "port"
#define MIMI_NVS_KEY_PROXY_TYPE "proxy_type"

/* WiFi Onboarding (Captive Portal) */
#define MIMI_ONBOARD_AP_PREFIX "ESPET-"
#define MIMI_ONBOARD_AP_PASS "" /* open network */
#define MIMI_ONBOARD_HTTP_PORT 80
#define MIMI_ONBOARD_DNS_STACK (4 * 1024)
#define MIMI_ONBOARD_MAX_SCAN 20

/* WiFi Onboarding (BLE Provisioning) */
#define MIMI_BLE_PROV_PREFIX "PROV_ESPET_"
#define MIMI_BLE_PROV_RESTART_DELAY_MS 2000
