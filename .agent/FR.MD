# ESPET — Locked Functional Requirements (FR.MD)

> **Document Status**: **LOCKED & AUTHORITATIVE**  
> **Effective Date**: 2026-09-10  
> **Scope**: Persistent Memory, Multi-User Isolation, Local Vector RAG, Device-User Binding, and Backend Storage.  
> **Audience**: All autonomous agents, developers, and FYP contributors working on ESPET (MimiClaw).  
> **Governance Rule**: Any implementation touching backend storage, memory management, or RAG must strictly adhere to these specifications. Deviations require explicit user approval.

---

## 1. System Vision & Architectural Invariants

ESPET (Embodied Sensing & Personal Execution Terminal) is an edge-first, embodied AI assistant composed of an ESP32-S3 microcontroller, a Python FastAPI companion backend, and a React 19 web dashboard.

### Core Architectural Invariants
1. **Local-First & Zero Cloud DB Dependency**: The backend runs on a local machine (laptop / home server / mini-PC). All persistent data, caching, and vector memory **must remain 100% self-contained** in local SQLite (`db/espet.db`). External hosted cloud databases (e.g. Supabase, Firebase, AWS RDS) are strictly forbidden for core memory.
2. **Sub-10ms Memory Retrieval**: The ESP32-S3 ReAct agent loop cannot tolerate cloud roundtrip latencies (150–350ms) during tool execution. Memory retrieval must execute in-process within **1–5ms**.
3. **Zero External Embedding API Keys**: Vector embeddings for RAG must be generated locally on the host CPU using lightweight ONNX models (`fastembed`). No paid or cloud embedding endpoints (e.g. OpenAI `text-embedding-3-small`) may be required for baseline operation.
4. **Strict User Privacy & Multi-Tenancy**: The system must support individual users ("each particular user"). User memories must be strictly isolated at the database query level so that memories, preferences, and daily notes never leak across users.
5. **Physical Device-to-Owner Binding**: An ESP32 hardware device is owned by a specific user by default. The binding is durable across reboots and only cleared on a physical hard reset / re-provisioning.

---

## 2. Functional Requirements Specification

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          ESP32-S3 DEVICE (MimiClaw)                     │
│                                                                         │
│  Fast Local Memory: SPIFFS (SOUL.md, USER.md, active MEMORY.md)         │
│  Aged Daily Notes:   Auto-archive YYYY-MM-DD.md ──▶ POST /api/memory/archive
│  ReAct Agent Loop:   tool_use(memory_search, "...") ──▶ POST /api/mcp/execute
│                      tool_use(memory_save, "...")   ──▶ POST /api/mcp/execute
└────────────────────────────────────┬────────────────────────────────────┘
                                     │
                          WiFi (LAN) │ Auth: Bearer <device_api_key>
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                   ESPET BACKEND (FastAPI :8000)                         │
│                                                                         │
│  1. Device Auth & User Resolver:                                        │
│     Bearer device_key ──▶ lookup `devices` table ──▶ resolves `user_id` │
│                                                                         │
│  2. Local Embedding Engine:                                             │
│     fastembed (BAAI/bge-small-en-v1.5 / MiniLM, 384d, ONNX CPU)         │
│                                                                         │
│  3. Unified SQLite Database (`db/espet.db`):                            │
│     ┌─────────────────────────────────────────────────────────────────┐ │
│     │  RELATIONAL TABLES (Strict `user_id` Foreign Keys)              │ │
│     │  • users (id, name, created_at)                                 │ │
│     │  • devices (device_id PK, user_id FK, device_name, paired_at)   │ │
│     │  • documents (user_id, name, content, version)                  │ │
│     │  • memory_chunks (id PK, user_id FK, source, category, content) │ │
│     │  • chat_messages (id PK, user_id FK, role, content, ts)         │ │
│     │  • sync_events (id PK, user_id FK, ts, versions_json)           │ │
│     │─────────────────────────────────────────────────────────────────│ │
│     │  VECTOR VIRTUAL TABLE (`sqlite-vec`)                            │ │
│     │  • vec_memory (rowid PK, embedding float[384])                  │ │
│     │    KNN Query joined on memory_chunks.user_id = :current_user    │ │
│     │    ──▶ IMPOSSIBLE for User A to retrieve User B's memories      │ │
│     └─────────────────────────────────────────────────────────────────┘ │
└────────────────────────────────────▲────────────────────────────────────┘
                                     │
                                     │ Web REST API / WebSocket
┌────────────────────────────────────┴────────────────────────────────────┐
│                   REACT 19 + ANTD 6 WEB DASHBOARD                       │
│                                                                         │
│  • Memory View: Search memories with semantic similarity, edit, delete  │
│  • Profile View: Select / switch active user profile                    │
│  • Device View: View paired ESPET device and owner binding              │
└─────────────────────────────────────────────────────────────────────────┘
```

### FR-1: Unified SQLite Storage Engine
- **Single Shared Database**: All backend application data, configuration, chat history, and vector embeddings must be stored in a single SQLite database file: `espet-backend/db/espet.db`.
- **Concurrency & Resilience**:
  - SQLite must operate in **WAL (Write-Ahead Logging)** mode (`PRAGMA journal_mode = WAL`).
  - Database access must use `aiosqlite` with asynchronous operations and an `asyncio.Lock()` pattern to protect serialized writes.
  - Replaces legacy `data/config.json` (one-time migration on first run) and consolidates `tokens.db`.

### FR-2: Local Vector RAG & Zero-Leakage Search
- **Vector Engine**: Vector similarity search is executed natively in SQLite using the `sqlite-vec` extension (`vec0` virtual table).
- **Embedding Generation**:
  - Model: `BAAI/bge-small-en-v1.5` or `sentence-transformers/all-MiniLM-L6-v2`.
  - Dimensions: **384 float dimensions**.
  - Execution: In-process via `fastembed` running on ONNX Runtime (CPU).
  - Initialization: Lazy-loaded or loaded during FastAPI lifespan startup.
- **Strict Query Isolation Pattern**:
  A vector KNN query must **never** execute unconstrained. It must always join with `memory_chunks` filtering on `user_id`:
  ```sql
  SELECT c.id, c.content, c.category, c.created_at, v.distance
  FROM vec_memory v
  JOIN memory_chunks c ON c.id = v.rowid
  WHERE c.user_id = :target_user_id
    AND v.embedding MATCH :query_vector
    AND k = :top_k;
  ```
  This mathematically guarantees that User A's vector search cannot return User B's records, even if vectors are spatially close.

### FR-3: Identity & Device-to-User Binding
- **Physical Device Ownership**:
  - Each physical ESP32 device is bound to a single `user_id` in the `devices` table.
  - The device identity is derived from its unique hardware MAC address or its provisioned `device_api_key`.
- **Request Resolution (`Depends(get_current_user_id)`)**:
  - Inbound ESP32 requests present `Authorization: Bearer <device_api_key>`. The backend resolves this key to the registered `user_id`.
  - If no explicit mapping exists, it binds to `'default_user'` so existing single-device setups work out-of-the-box.
  - Web dashboard requests pass the active profile's `user_id` via header (`X-User-Id`) or session state.
- **Hard Reset & Re-binding**:
  - When an ESP32 is factory-reset (erasing NVS), it re-enters onboarding captive portal mode.
  - The web dashboard or pairing API allows claiming and reassigning an unassigned or reset device.

### FR-4: Dual-Pipeline Memory Ingestion
Memory ingestion must occur through three complementary channels:

1. **Aged Daily Notes Archival (Automated)**:
   - The ESP32 maintains recent daily episodic notes (`YYYY-MM-DD.md`) in `/spiffs/memory/`.
   - Notes older than a configurable threshold (e.g. 3 days) are uploaded to `POST /api/memory/archive`.
   - The backend chunks the markdown note, computes embeddings via `fastembed`, inserts chunks into `memory_chunks`, and populates `vec_memory`.
   - Upon successful archival confirmation (`200 OK`), the ESP32 unlinks the aged daily note from SPIFFS to conserve flash space.

2. **ReAct Agent Memory Tools (Conversational)**:
   - `memory_search(query: str, limit: int = 5)`: Remote MCP/custom tool called by the LLM during conversation. Embeds the query, runs the user-isolated KNN search, and returns concise relevant snippets to the LLM.
   - `memory_save(content: str, category: str = "fact|preference|event")`: Remote tool called by the LLM when explicit user facts, instructions, or preferences are expressed (e.g. "Remember that my advisor is Dr Wong"). Immediately chunked, embedded, and stored.

3. **Web Dashboard Management (Curation)**:
   - The React dashboard Memory view provides a search bar that executes semantic similarity search via `POST /api/memory/search`.
   - Users can manually create, edit, or delete memory chunks. Deleting a chunk automatically removes its corresponding embedding from `vec_memory`.

---

## 3. Authoritative SQLite Database Schema (`db/espet.db`)

```sql
-- PRAGMA configuration
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- 1. Users Table
CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 2. Devices Table (Binding physical hardware to owner)
CREATE TABLE IF NOT EXISTS devices (
    device_id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    device_name TEXT DEFAULT 'ESPET Device',
    device_key_hash TEXT,
    last_seen_at TIMESTAMP,
    paired_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 3. Document Store (SOUL.md, USER.md, MEMORY.md per user)
CREATE TABLE IF NOT EXISTS documents (
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    content TEXT NOT NULL,
    version INTEGER NOT NULL DEFAULT 1,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, name)
);

-- 4. Memory Chunks Table (Metadata & Raw Text)
CREATE TABLE IF NOT EXISTS memory_chunks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    source_type TEXT NOT NULL,         -- 'daily_note', 'agent_tool', 'web_ui'
    category TEXT DEFAULT 'general',   -- 'preference', 'fact', 'event', 'project'
    content TEXT NOT NULL,
    metadata_json TEXT,                -- optional JSON string (date, tags, context)
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 5. Vector Virtual Table (sqlite-vec)
CREATE VIRTUAL TABLE IF NOT EXISTS vec_memory USING vec0(
    embedding float[384]
);

-- 6. Chat History Table (Durable multi-session logs)
CREATE TABLE IF NOT EXISTS chat_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    chat_id TEXT NOT NULL,
    role TEXT NOT NULL,                -- 'user', 'assistant', 'system', 'tool'
    content TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 7. Scheduled Cron Jobs
CREATE TABLE IF NOT EXISTS jobs (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    job_json TEXT NOT NULL,
    version INTEGER NOT NULL DEFAULT 1,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 8. Skills Table
CREATE TABLE IF NOT EXISTS skills (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    filename TEXT NOT NULL,
    content TEXT NOT NULL,
    enabled BOOLEAN DEFAULT 1,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 9. Sync Events (Sync-activity sparkline / telemetry)
CREATE TABLE IF NOT EXISTS sync_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    device_id TEXT REFERENCES devices(device_id),
    ts INTEGER NOT NULL,
    versions_json TEXT NOT NULL
);

-- 10. OAuth Tokens (Consolidated from tokens.db)
CREATE TABLE IF NOT EXISTS oauth_tokens (
    user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    token_data TEXT NOT NULL,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

---

## 4. API Surface for Memory & RAG

| Method | Route | Auth | Payload / Params | Purpose |
|---|---|---|---|---|
| `POST` | `/api/memory/archive` | Device Key | `{ "date": "YYYY-MM-DD", "content": "..." }` | Uploads aged daily note from SPIFFS to SQLite RAG |
| `POST` | `/api/mcp/execute` | Device Key | `{ "tool_name": "memory_search", "arguments": { "query": "...", "limit": 5 } }` | Agent ReAct search tool (returns top-K snippets) |
| `POST` | `/api/mcp/execute` | Device Key | `{ "tool_name": "memory_save", "arguments": { "content": "...", "category": "fact" } }` | Agent ReAct save tool (chunks, embeds, and stores) |
| `GET` | `/api/ui/memory` | UI Auth / User | `?query=...&category=...` | Dashboard semantic search and list of memory chunks |
| `POST` | `/api/ui/memory` | UI Auth / User | `{ "content": "...", "category": "..." }` | Dashboard manual memory insertion |
| `DELETE` | `/api/ui/memory/{id}` | UI Auth / User | — | Dashboard memory chunk deletion (removes vector row) |
| `GET` | `/api/ui/devices` | UI Auth / User | — | List paired devices and their bound `user_id` |
| `POST` | `/api/ui/devices/bind` | UI Auth / User | `{ "device_id": "...", "user_id": "..." }` | Rebind a device to a user |

---

## 5. Non-Functional Requirements & Constraints

1. **Memory Budget**: Host CPU memory usage for `fastembed` must remain under 300MB. Model files are cached in `~/.cache/fastembed/`.
2. **Firmware Compatibility**: The ESP32 firmware requires zero modifications to its core network protocol; it interacts with `memory_search` and `memory_save` through the existing generic `POST /api/mcp/execute` router.
3. **Graceful Fallback**: If an embedding cannot be generated or the vector engine fails, the system must log the error and fall back to SQLite `LIKE` keyword search without crashing the request.
4. **Data Portability**: Since all data is contained in `db/espet.db`, full database backups can be taken via SQLite `.backup` or simple file copy when WAL checkpoints are idle.
