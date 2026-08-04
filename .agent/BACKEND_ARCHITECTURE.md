# ESPET Backend Server — Architecture & Logic

> **Purpose**: Detailed technical reference for the ESPET Customized Backend Server.
> **Last updated**: 2026-08-04
> **Status**: Design Phase (not yet built — see AGENTS v1.MD §6)

---

## 1. What the Backend Server Does

The ESPET Backend Server is the **bridge** between the ESP32 hardware device and external cloud services. It serves three core purposes:

| Role | Description |
|------|-------------|
| **MCP Host** | Runs and manages multiple MCP (Model Context Protocol) servers (Gmail, Google Docs, Calendar, etc.) |
| **Tool Router** | Receives tool call requests from the ESP32 agent and routes them to the correct MCP server |
| **Auth Manager** | Securely stores and manages OAuth tokens and API keys for 3rd-party services on behalf of the user |

The ESP32 is too resource-constrained (16MB flash, 8MB PSRAM) to run Python/Node.js MCP servers or handle OAuth flows. The backend handles all of that.

---

## 2. High-Level Architecture

```
┌───────────────────────────────────────────────────────────────────────┐
│                          ESP32-S3 (ESPETx )                             │
│                                                                       │
│  User speaks / presses button / sensor triggers                       │
│       │                                                               │
│       ▼                                                               │
│  ┌─────────────────────────────────────────────────┐                  │
│  │  MimiClaw Agent Loop (Core 1) — ReAct Pattern   │                  │
│  │  1. Build context (SOUL + USER + MEMORY + input) │                  │
│  │  2. Send to LLM                                  │                  │
│  │  3. Receive tool_use or text response             │                  │
│  │  4. If tool_use → dispatch tool                   │                  │
│  │  5. Loop until end_turn (max 10 iterations)       │                  │
│  └──────────────┬──────────────────┬────────────────┘                  │
│                 │                  │                                    │
│      ┌──────────▼──────┐   ┌──────▼──────────┐                        │
│      │  Local Tools    │   │  Remote Tools   │                        │
│      │  (GPIO, OLED,   │   │  (Gmail, Docs,  │                        │
│      │  SPIFFS, Cron)  │   │  Calendar, etc) │                        │
│      │  Runs on ESP32  │   │  Calls Backend  │                        │
│      └─────────────────┘   └────────┬────────┘                        │
└─────────────────────────────────────┼─────────────────────────────────┘
                                      │
                                      │  HTTP POST /api/mcp/execute
                                      │  (WiFi — single endpoint)
                                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      CUSTOMIZED BACKEND SERVER                          │
│                                                                         │
│  ┌────────────────────────────────────────────────────────────┐         │
│  │  POST /api/mcp/execute  (Single Generic Endpoint)          │         │
│  │                                                            │         │
│  │  Receives: { "tool_name": "...", "arguments": {...} }      │         │
│  │  Routes to correct MCP Server automatically                │         │
│  └────────────────────────┬───────────────────────────────────┘         │
│                           │                                             │
│               ┌───────────┼───────────┐                                 │
│               ▼           ▼           ▼                                 │
│  ┌────────────────┐ ┌──────────┐ ┌──────────────┐                      │
│  │  MCP Server:   │ │  MCP:    │ │  MCP Server:  │  ... (add more)     │
│  │  Gmail         │ │  Docs    │ │  Calendar     │                      │
│  │                │ │          │ │               │                      │
│  │  Tools:        │ │  Tools:  │ │  Tools:       │                      │
│  │  • send_email  │ │  • read  │ │  • create     │                      │
│  │  • read_email  │ │  • write │ │  • list       │                      │
│  │  • search      │ │  • list  │ │  • delete     │                      │
│  └────────┬───────┘ └────┬─────┘ └──────┬───────┘                      │
│           │              │              │                               │
│           ▼              ▼              ▼                               │
│  ┌──────────────────────────────────────────────────┐                   │
│  │            Auth Manager (Token Store)             │                   │
│  │  Stores OAuth refresh/access tokens per user      │                   │
│  │  Handles token refresh automatically              │                   │
│  └──────────────────────────────────────────────────┘                   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. The Single Endpoint Pattern

### Why ONE endpoint instead of many?

| Approach | Example Routes | Problem |
|----------|---------------|---------|
| ❌ Traditional REST | `/api/gmail/send`, `/api/gmail/read`, `/api/docs/read`, `/api/docs/write`, `/api/calendar/create` ... | You must write and maintain a separate endpoint for every single action. Adding a new MCP server = writing more routes. ESP32 firmware must be updated with new URLs. |
| ✅ Single MCP Router | `/api/mcp/execute` | ONE route handles ALL tools from ALL MCP servers. Adding a new MCP server = zero code change to backend route or ESP32 firmware. |

### How it works

The ESP32 sends **one HTTP request** to the backend with the `tool_name` and `arguments` inside the JSON body. The backend MCP client automatically looks up which MCP server owns that tool and forwards the request.

**Request from ESP32:**
```http
POST /api/mcp/execute
Content-Type: application/json
Authorization: Bearer <device_api_key>

{
  "tool_name": "gmail_send_email",
  "arguments": {
    "to": "supervisor@tar.edu.my",
    "subject": "FYP Progress Update",
    "body": "Hello Dr Wong, here is the latest update..."
  }
}
```

**Response to ESP32:**
```json
{
  "success": true,
  "result": {
    "message_id": "msg_abc123",
    "status": "sent"
  }
}
```

### Backend handler (pseudo-code)

```python
@app.post("/api/mcp/execute")
async def execute_tool(request):
    tool_name = request.json["tool_name"]
    arguments = request.json["arguments"]

    # mcp_client automatically knows which MCP server to call.
    # No if/else, no switch statement, no hardcoded tool names.
    result = await mcp_client.call_tool(tool_name, arguments)

    return {"success": True, "result": result}
```

**Key point**: You do NOT write `if tool_name == "gmail_send_email"` anywhere. The MCP Client library handles tool routing internally via its auto-discovered tool registry.

---

## 4. How the LLM, ESP32, and Backend Work Together

### Step-by-step flow for: *"Email Dr Wong a summary of my proposal doc"*

```
Step 1: User says "Email Dr Wong a summary of my proposal doc"
        ──► ESP32 captures voice via INMP441 mic
        ──► Sends audio to Speech-to-Text API
        ──► Gets text transcription

Step 2: MimiClaw agent loop builds context prompt:
        System prompt = SOUL.md + USER.md + MEMORY.md + available tools list
        User message = "Email Dr Wong a summary of my proposal doc"
        ──► Sends to Cloud LLM (e.g., Gemini)

Step 3: LLM THINKS (iteration 1):
        "I need to read the proposal document first."
        ──► Returns: tool_use("google_docs_read", {"document_id": "proposal_123"})

Step 4: ESP32 receives tool_use, sees it's a remote tool
        ──► Sends POST /api/mcp/execute to Backend Server
            { "tool_name": "google_docs_read", "arguments": {"document_id": "proposal_123"} }

Step 5: Backend routes to mcp-docs server
        ──► mcp-docs reads the Google Doc using stored OAuth token
        ──► Returns document content to ESP32

Step 6: MimiClaw feeds tool result back to LLM (iteration 2)
        LLM THINKS: "Now I have the content. Let me summarize and email it."
        ──► Returns: tool_use("gmail_send_email", {
              "to": "drwong@tar.edu.my",
              "subject": "Proposal Summary",
              "body": "Dear Dr Wong, here is the summary: ..."
            })

Step 7: ESP32 sends POST /api/mcp/execute to Backend Server
        ──► Backend routes to mcp-gmail server
        ──► mcp-gmail sends email using stored OAuth token
        ──► Returns {"status": "sent"}

Step 8: MimiClaw feeds tool result back to LLM (iteration 3)
        LLM THINKS: "Done! Email sent successfully."
        ──► Returns: end_turn("I've read your proposal and emailed a summary to Dr Wong.")

Step 9: ESP32 outputs response:
        ──► Speaks via MAX98357A speaker
        ──► Displays on SSD1306 OLED
```

> **Notice**: The backend code NEVER changed. The LLM decided which tools to call and in what order. The backend just forwarded each request to the correct MCP server.

---

## 5. MCP Server Auto-Discovery (How the Backend Knows All Tools)

When the backend server starts up, the MCP Client connects to every registered MCP server and asks: *"What tools do you offer?"*

Each MCP server responds with a list of tool schemas (name, description, required parameters). The MCP Client merges all of these into one unified tool registry.

```
┌─────────────────────────────────────────────────────────────────┐
│                    Backend Server Startup                        │
│                                                                 │
│  1. Connect to mcp-gmail server                                 │
│     ──► Discovers: gmail_send_email, gmail_read_email,          │
│                    gmail_search_threads, gmail_list_labels       │
│                                                                 │
│  2. Connect to mcp-google-docs server                           │
│     ──► Discovers: docs_read_document, docs_write_document,     │
│                    docs_list_documents, docs_create_document     │
│                                                                 │
│  3. Connect to mcp-google-calendar server                       │
│     ──► Discovers: calendar_create_event, calendar_list_events, │
│                    calendar_delete_event                         │
│                                                                 │
│  Unified Tool Registry (auto-built, no code needed):            │
│  ┌────────────────────────┬──────────────────────────┐          │
│  │ Tool Name              │ Routed To                │          │
│  ├────────────────────────┼──────────────────────────┤          │
│  │ gmail_send_email       │ mcp-gmail server         │          │
│  │ gmail_read_email       │ mcp-gmail server         │          │
│  │ gmail_search_threads   │ mcp-gmail server         │          │
│  │ docs_read_document     │ mcp-google-docs server   │          │
│  │ docs_write_document    │ mcp-google-docs server   │          │
│  │ docs_create_document   │ mcp-google-docs server   │          │
│  │ calendar_create_event  │ mcp-google-calendar      │          │
│  │ calendar_list_events   │ mcp-google-calendar      │          │
│  │ ...                    │ ...                      │          │
│  └────────────────────────┴──────────────────────────┘          │
│                                                                 │
│  This registry is also sent to the LLM as the "available tools" │
│  list so the LLM knows what it can call.                        │
└─────────────────────────────────────────────────────────────────┘
```

### Adding a new MCP server in the future

To add a new capability (e.g., Notion), you just:

1. Install the MCP server: `npx @anthropic/mcp-server-notion` (or similar)
2. Add it to your backend config file
3. Restart the backend

**That's it.** No new routes. No new code. No ESP32 firmware update. The new Notion tools automatically appear in the tool registry and the LLM can immediately start using them.

---

## 6. Authentication & Token Management

### Overview

The backend server manages ALL third-party authentication on behalf of the user. The ESP32 never stores or handles OAuth tokens for Google, etc.

```
┌──────────────────────────────────────────────────────────────────┐
│                   Authentication Architecture                     │
│                                                                  │
│  ┌──────────────────────────────┐                                │
│  │  ESP32 → Backend Auth       │                                │
│  │  Simple API Key / Token     │  (device_api_key in header)    │
│  │  Just to verify the ESP32   │                                │
│  │  is authorized to call the  │                                │
│  │  backend.                   │                                │
│  └──────────────────────────────┘                                │
│                                                                  │
│  ┌──────────────────────────────┐                                │
│  │  Backend → 3rd Party Auth   │                                │
│  │  Full OAuth2 flow           │  (stored in backend database)  │
│  │  • Google OAuth tokens      │                                │
│  │  • Refresh tokens           │                                │
│  │  • API keys (Brave, etc.)   │                                │
│  └──────────────────────────────┘                                │
└──────────────────────────────────────────────────────────────────┘
```

### OAuth Setup Flow (One-Time per User)

```
Step 1: User opens browser → http://backend-server:8000/auth/google
Step 2: Backend redirects to Google OAuth consent screen
Step 3: User logs in with Google account and grants permissions
Step 4: Google redirects back to backend with auth code
Step 5: Backend exchanges auth code for access_token + refresh_token
Step 6: Backend stores tokens in database (encrypted)
Step 7: Done! MCP servers can now use these tokens automatically.
```

### Token Refresh (Automatic)

When an access token expires (typically after 1 hour), the backend automatically:
1. Detects the expired token (HTTP 401 from Google)
2. Uses the stored refresh token to get a new access token
3. Stores the new access token
4. Retries the failed request

The ESP32 and the user never need to re-authenticate.

---

## 7. Backend Project Structure

```
espet-backend/
│
├── main.py                       # App entry point, starts server + MCP clients
├── requirements.txt              # Python dependencies
├── .env                          # Environment variables (secrets, ports)
│
├── config/
│   └── mcp_servers.json          # List of MCP servers to connect to
│
├── api/
│   ├── mcp_router.py             # POST /api/mcp/execute — the ONE endpoint
│   ├── auth_router.py            # OAuth login/callback routes
│   ├── tools_router.py           # GET /api/tools — returns available tool list
│   └── health.py                 # GET /health — server health check
│
├── mcp/
│   ├── client.py                 # MCP Client — connects to MCP servers
│   ├── registry.py               # Tool Registry — auto-built from MCP discovery
│   └── servers/                  # MCP server configs or local server scripts
│       ├── gmail.json            # Config for Gmail MCP server
│       ├── google_docs.json      # Config for Google Docs MCP server
│       └── google_calendar.json  # Config for Google Calendar MCP server
│
├── auth/
│   ├── oauth_manager.py          # Google OAuth2 flow (login, callback, refresh)
│   └── token_store.py            # Encrypted token storage (SQLite / file-based)
│
├── db/
│   └── tokens.db                 # SQLite database for OAuth tokens
│
└── tests/
    ├── test_mcp_router.py        # Test tool routing
    └── test_auth.py              # Test OAuth flow
```

### Key Files Explained

| File | Purpose |
|------|---------|
| `main.py` | Starts the FastAPI/Flask server. On startup, connects to all MCP servers listed in `mcp_servers.json` and builds the tool registry. |
| `mcp_router.py` | The **single generic endpoint** (`POST /api/mcp/execute`). Receives `tool_name` + `arguments`, forwards to MCP client. No tool-specific logic. |
| `tools_router.py` | Returns the full list of available tools (name, description, parameter schema). The ESP32 agent loop uses this to tell the LLM what tools are available. |
| `client.py` | MCP Client that manages connections to multiple MCP servers. Handles `call_tool()` routing. |
| `registry.py` | Auto-built lookup table mapping tool names to their MCP server. Created at startup during discovery. |
| `oauth_manager.py` | Handles Google OAuth2 consent flow. The user visits a web page once to grant access. |
| `token_store.py` | Securely stores and retrieves OAuth tokens. Auto-refreshes expired tokens. |
| `mcp_servers.json` | Config file listing which MCP servers to connect to at startup. |

---

## 8. MCP Server Configuration

### `config/mcp_servers.json`

This is the **only file you edit** when adding new MCP capabilities:

```json
{
  "servers": [
    {
      "name": "gmail",
      "command": "npx",
      "args": ["-y", "@anthropic/mcp-server-gmail"],
      "env": {
        "GOOGLE_CLIENT_ID": "${GOOGLE_CLIENT_ID}",
        "GOOGLE_CLIENT_SECRET": "${GOOGLE_CLIENT_SECRET}"
      }
    },
    {
      "name": "google-docs",
      "command": "npx",
      "args": ["-y", "@anthropic/mcp-server-google-docs"],
      "env": {
        "GOOGLE_CLIENT_ID": "${GOOGLE_CLIENT_ID}",
        "GOOGLE_CLIENT_SECRET": "${GOOGLE_CLIENT_SECRET}"
      }
    },
    {
      "name": "google-calendar",
      "command": "npx",
      "args": ["-y", "@anthropic/mcp-server-google-calendar"],
      "env": {
        "GOOGLE_CLIENT_ID": "${GOOGLE_CLIENT_ID}",
        "GOOGLE_CLIENT_SECRET": "${GOOGLE_CLIENT_SECRET}"
      }
    }
  ]
}
```

### Adding a new MCP server

To add Notion support, just append to the `servers` array:

```json
{
  "name": "notion",
  "command": "npx",
  "args": ["-y", "@anthropic/mcp-server-notion"],
  "env": {
    "NOTION_API_KEY": "${NOTION_API_KEY}"
  }
}
```

Restart the backend. Done. The LLM can now read and write Notion pages without any code changes.

---

## 9. API Endpoints Summary

| Method | Endpoint | Purpose | Called By |
|--------|----------|---------|-----------|
| `POST` | `/api/mcp/execute` | Execute any MCP tool | ESP32 agent loop |
| `GET` | `/api/tools` | List all available tools + schemas | ESP32 (at boot or on-demand) |
| `GET` | `/auth/google` | Start Google OAuth consent flow | User (browser, one-time) |
| `GET` | `/auth/google/callback` | OAuth callback (Google redirects here) | Google OAuth |
| `GET` | `/health` | Server health check | ESP32 / monitoring |

### Endpoint Details

#### `POST /api/mcp/execute`

**The most important endpoint.** This is the only endpoint the ESP32 calls during normal operation.

**Request:**
```json
{
  "tool_name": "string",       // Name of the MCP tool to call
  "arguments": { }             // Tool-specific arguments (varies per tool)
}
```

**Response (success):**
```json
{
  "success": true,
  "result": { }                // Tool-specific response data
}
```

**Response (error):**
```json
{
  "success": false,
  "error": "Tool 'xyz' not found in registry"
}
```

#### `GET /api/tools`

Returns the full list of discovered MCP tools so the ESP32 can include them in the LLM system prompt.

**Response:**
```json
{
  "tools": [
    {
      "name": "gmail_send_email",
      "description": "Send an email via Gmail",
      "parameters": {
        "to": { "type": "string", "required": true },
        "subject": { "type": "string", "required": true },
        "body": { "type": "string", "required": true }
      }
    },
    {
      "name": "docs_read_document",
      "description": "Read a Google Doc by ID",
      "parameters": {
        "document_id": { "type": "string", "required": true }
      }
    }
  ]
}
```

---

## 10. How ESP32 Decides: Local Tool vs. Remote Tool

Inside the MimiClaw agent loop on the ESP32, when the LLM returns a `tool_use` response, the ESP32 checks if the tool exists locally or needs to go to the backend:

```c
// Pseudo-code (C, on ESP32)

if (tool_exists_locally(tool_name)) {
    // Tools like: gpio_write, oled_display, read_file, cron_add
    result = execute_local_tool(tool_name, arguments);
} else {
    // Tools like: gmail_send_email, docs_read_document, calendar_create_event
    result = http_post("http://backend:8000/api/mcp/execute", {
        "tool_name": tool_name,
        "arguments": arguments
    });
}

// Feed result back to LLM for next iteration
```

### Tool Ownership Table

| Tool | Runs On | Why |
|------|---------|-----|
| `gpio_read` / `gpio_write` | ESP32 (local) | Direct hardware access needed |
| `oled_display` | ESP32 (local) | Direct I²C hardware control |
| `read_file` / `write_file` | ESP32 (local) | SPIFFS filesystem on flash |
| `cron_add` / `cron_list` | ESP32 (local) | Local scheduler |
| `get_current_time` | ESP32 (local) | Simple HTTP call |
| `web_search` | ESP32 (local) or Backend | Can work from either |
| `gmail_send_email` | Backend (remote) | Needs OAuth tokens + Gmail API |
| `gmail_read_email` | Backend (remote) | Needs OAuth tokens + Gmail API |
| `docs_read_document` | Backend (remote) | Needs OAuth tokens + Docs API |
| `docs_write_document` | Backend (remote) | Needs OAuth tokens + Docs API |
| `calendar_create_event` | Backend (remote) | Needs OAuth tokens + Calendar API |

---

## 11. Tech Stack (Recommended)

| Layer | Technology | Rationale |
|-------|-----------|-----------|
| **Language** | Python 3.11+ | Fast to develop, rich MCP SDK support |
| **Web Framework** | FastAPI | Async, fast, auto-generates API docs |
| **MCP SDK** | `mcp` (Python SDK) | Official MCP client library |
| **OAuth** | `google-auth-oauthlib` | Google's official OAuth library |
| **Token Storage** | SQLite | Lightweight, no external database needed |
| **Deployment** | Local PC / Raspberry Pi / VPS | Same network as ESP32, or cloud |

---

## 12. Security Considerations

| Concern | Mitigation |
|---------|------------|
| ESP32 → Backend auth | Use a shared API key in the `Authorization` header. Only the ESP32 should know this key. |
| OAuth tokens | Store encrypted in SQLite. Never expose tokens in API responses. |
| Network | Use HTTPS between ESP32 and backend (or at minimum, keep on same local network during development). |
| MCP server isolation | Each MCP server runs as a separate subprocess. One server crashing doesn't bring down the backend. |
| Rate limiting | Add basic rate limiting on `/api/mcp/execute` to prevent runaway agent loops. |

---

## 13. Development Roadmap

| Phase | Task | Priority |
|-------|------|----------|
| **Phase 1** | Set up FastAPI project + `/health` endpoint | P0 |
| **Phase 2** | Implement MCP Client + auto-discovery + tool registry | P0 |
| **Phase 3** | Implement `/api/mcp/execute` single endpoint | P0 |
| **Phase 4** | Implement `/api/tools` endpoint | P0 |
| **Phase 5** | Google OAuth flow (`/auth/google` + callback) | P0 |
| **Phase 6** | Connect Gmail MCP server + test send/read email | P1 |
| **Phase 7** | Connect Google Calendar MCP server | P1 |
| **Phase 8** | Connect Google Docs MCP server | P1 |
| **Phase 9** | Connect ESP32 → Backend pipeline (test full loop) | P0 |
| **Phase 10** | Add more MCP servers as needed | P2 |
