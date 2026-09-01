# ESPET Backend Server

MCP tool router for the ESPET ESP32-S3 embodied AI agent. Routes tool calls
from the ESP32 agent loop to Google's official remote MCP servers
(Gmail, Calendar, Drive).

## Architecture

```
ESP32 (Mimiclaw Agent Loop)
  │
  │  POST /api/mcp/execute
  │  { "tool_name": "gmail_send_email", "arguments": {...} }
  │
  ▼
┌─────────────────────────────────────────┐
│         ESPET Backend Server            │
│                                         │
│  FastAPI + MCP Client + OAuth Manager   │
│                                         │
│  Auto-discovers tools from:             │
│  • Gmail MCP (gmailmcp.googleapis.com)  │
│  • Calendar MCP (calendarmcp.*)         │
│  • Drive MCP (drivemcp.*)              │
│                                         │
│  Routes tool calls automatically.       │
│  No tool-specific code needed.          │
└─────────────────────────────────────────┘
```

## Quick Start

### 1. Prerequisites
- Python 3.11+
- A Google Cloud project with Gmail/Calendar/Drive APIs enabled
- OAuth 2.0 credentials (Desktop app type)

### 2. Setup

```bash
cd espet-backend

# Create virtual environment
python3 -m venv venv
source venv/bin/activate   # macOS/Linux
# venv\Scripts\activate    # Windows

# Install dependencies
pip install -r requirements.txt

# Configure environment
cp .env.example .env
# Edit .env with your Google OAuth credentials and device API key
```

### 3. Run

```bash
python main.py
# or
uvicorn main:app --reload --port 8000
```

### 4. Authenticate with Google (One-Time)

Open in browser: `http://localhost:8000/auth/google`

Grant permissions for Gmail, Calendar, and Drive. Tokens are stored
automatically and refreshed forever.

### 5. Test

```bash
# Health check
curl http://localhost:8000/health

# Check auth status
curl http://localhost:8000/auth/status

# List available tools (requires API key)
curl -H "Authorization: Bearer YOUR_DEVICE_API_KEY" \
     http://localhost:8000/api/tools

# Execute a tool (requires API key)
curl -X POST http://localhost:8000/api/mcp/execute \
     -H "Authorization: Bearer YOUR_DEVICE_API_KEY" \
     -H "Content-Type: application/json" \
     -d '{"tool_name":"gmail_search_threads","arguments":{"query":"from:supervisor"}}'
```

## API Endpoints

| Method | Endpoint | Auth | Purpose |
|--------|----------|------|---------|
| `GET` | `/` | None | Web dashboard (control center) |
| `GET` | `/health` | None | Server health + MCP status |
| `GET` | `/auth/google` | None | Start OAuth flow (browser) |
| `GET` | `/auth/google/callback` | None | OAuth callback (Google) |
| `GET` | `/auth/status` | None | Check if authenticated |
| `GET` | `/api/tools` | API Key | List all available tools |
| `POST` | `/api/mcp/execute` | API Key | Execute any MCP tool |
| `POST` | `/api/mcp/reconnect` | API Key | Reconnect all MCP servers (after Google re-auth) |
| `GET` | `/api/config` | API Key | Full config bundle (device pull) |
| `GET` | `/api/config/version` | API Key | Per-section config versions |
| `PUT` | `/api/ui/profile` | API Key | Update USER.md |
| `PUT` | `/api/ui/soul` | API Key | Update SOUL.md |
| `PUT` | `/api/ui/memory` | API Key | Update MEMORY.md |
| `PUT` | `/api/ui/device` | API Key | Device name / timezone / sync interval |
| `GET` | `/api/ui/jobs` | API Key | List web cron jobs |
| `POST` | `/api/ui/jobs` | API Key | Create cron job |
| `PUT` | `/api/ui/jobs/{id}` | API Key | Edit cron job |
| `DELETE` | `/api/ui/jobs/{id}` | API Key | Delete cron job |
| `GET` | `/api/ui/avatar` | API Key | Agent avatar dress-up state (dashboard-only) |
| `PUT` | `/api/ui/avatar` | API Key | Update agent avatar |

## Config Sync (Dashboard → Device)

The web dashboard edits a file-backed config store (`data/config.json`,
seeded with the `spiffs_data/` defaults on first run). Each section
(`user`, `soul`, `memory`, `device`, `jobs`) has its own version counter
that is bumped on every edit.

On the firmware side, `main/sync/config_sync.c` runs a background task that:

1. Pulls `GET /api/config` once shortly after boot and every
   `sync_interval_s` (configurable from the Device page, default 5 min).
2. Applies only sections whose version increased — writing USER.md,
   SOUL.md and MEMORY.md to SPIFFS, applying the timezone via
   `setenv("TZ") + tzset()`, and merging web cron jobs into
   `/spiffs/cron.json`.
3. Tracks last-applied versions in `/spiffs/config/sync_state.json`.

Cron merge rule: only jobs previously received from the web are replaced
or removed. Jobs the agent created on-device (e.g. "remind me at 5pm" by
voice) are preserved across syncs.

## Adding New MCP Servers

Edit `config/mcp_servers.json` and add a new entry:

```json
{
  "name": "notion",
  "url": "https://notion-mcp-server.example.com/mcp/v1",
  "description": "Notion workspace access"
}
```

Restart the server. Done. No code changes needed.

## Project Structure

```
espet-backend/
├── main.py                 # FastAPI app entry point
├── config.py               # Settings from .env
├── requirements.txt        # Python dependencies
├── .env.example            # Environment template
│
├── config/
│   └── mcp_servers.json    # MCP server registry
│
├── api/
│   ├── mcp_router.py       # POST /api/mcp/execute
│   ├── tools_router.py     # GET /api/tools
│   ├── auth_router.py      # OAuth endpoints
│   └── health.py           # Health check
│
├── mcp/
│   ├── client.py           # MCP Client Manager
│   └── registry.py         # Tool Registry
│
├── auth/
│   ├── device_auth.py      # API key middleware
│   ├── oauth_manager.py    # Google OAuth flow
│   └── token_store.py      # Token storage (SQLite)
│
└── db/
    └── tokens.db           # OAuth tokens (auto-created)
```
