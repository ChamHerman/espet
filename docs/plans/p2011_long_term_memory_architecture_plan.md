# Implementation Plan: Robust, High-Salience Long-Term Memory Architecture

This document establishes the technical design and phased implementation plan for transforming ESPET's memory system from an uncurated, action-polluted store into a robust, storage-bounded, multi-tiered semantic memory engine.

---

## 1. Goal Description

Currently, ESPET has a partial three-tier memory setup (on-device SPIFFS `MEMORY.md`/`jsonl` sessions, backend SQLite-Vec RAG, and MCP `memory_save`/`memory_search` tools), but suffers from several critical architectural bottlenecks:
1. **Vector Semantic Pollution & Bloat**: `cloud_router.py` automatically intercepts tool calls and logs strings like `Action: executed tool gpio_write...` directly into `vec_memories`. This pollutes vector search results (displacing actual user facts with hardware toggles) and wastes SQLite storage with 384-dimensional float vectors for trivial actions.
2. **Uncalibrated Extraction & Knowledge Contradiction**: `MemoryOrganizer` lacks an explicit durability/salience rubric, occasionally storing transient chatter while missing multi-turn facts or accumulating contradictory preferences (e.g. "User likes coffee" alongside "User quit caffeine").
3. **Storage Imbalance**: While ESP32 flash is constrained (11.8 MB SPIFFS, < 4 KB prompt memory budget), there is no automated distillation pipeline to distill backend knowledge into the device's hot `MEMORY.md`.
4. **Attention Dilution during LLM Turns**: Vector search queries use raw, un-contextualized user utterances (failing when queries contain pronouns like "what's his email?"), and retrieved memories are placed without explicit priming instructions, causing smaller LLMs to ignore retrieved context.

### Objectives:
- **Decouple Action Logs from Vector RAG**: Move tool invocation traces to an ephemeral rolling execution audit log table (`action_logs` with 7-day TTL) and completely remove raw tool calls from SQLite-Vec.
- **Implement 3-Factor Salience Rubric & Conflict Resolution**: Upgrade `MemoryOrganizer` to score facts by Durability (>30 days), Actionability, and User Relevance, with automatic upsert/superseding of contradictory facts.
- **Hierarchical Memory Tiering & Flash Distillation**: Keep on-device `MEMORY.md` strictly bounded (< 3 KB) with an automated distillation job that keeps the device flash synced with highest-priority core profile and active facts.
- **Context-Aware Retrieval & Explicit Priming**: Resolve pronouns against recent turn history before vector queries, and format retrieved memories into a prominent `## Established User Context (Priority: Follow Strictly)` block directly above the active turn.
- **Agent Memory Control Tools**: Equip the agent with `memory_update` and `memory_forget` MCP tools alongside existing `memory_save` and `memory_search`.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            ESPET Memory Pipeline                            │
└─────────────────────────────────────────────────────────────────────────────┘

 [User Message / Hardware Event]
               │
               ▼
 [Context-Aware Query Rewriter] ──► (Resolves pronouns from last 2 turns)
               │
               ▼
 [RAG Semantic Retrieval] ───────► (Queries SQLite-Vec: category != 'action')
               │
               ▼
 [Priming Prompt Assembly] ──────► [## Established User Context (Strict Priority)]
               │
               ▼
 [Primary LLM ReAct Loop] ───────► (Invokes tools: memory_save, memory_forget, etc.)
               │
               ├────────────────────────────────────────┬─────────────────────────┐
               ▼                                        ▼                         ▼
   [State-Changing Actions]                 [Tool Execution Log]      [Post-Turn Async Analyzer]
   (Emails sent, Cal events)                 (Rolling 7-day TTL)          (MemoryOrganizer)
               │                                                                  │
               ▼                                                                  ▼
    [Promote to Event Memory]                                         [3-Factor Salience Rubric]
               │                                                                  │
               ▼                                                                  ▼
  [SQLite-Vec (vec_memories)] ◄─────────────────────────────────────── [Conflict Resolution & Upsert]
               │
               ▼
 [Automated Distillation Engine] ──► Pushes top curated facts ──► [On-Device MEMORY.md (<3KB)]
```

---

## 2. User Review Required

> [!IMPORTANT]
> **Action Log Migration & Cleanup**: Existing `source_type='action'` records currently occupying space in `memory_chunks` and `vec_memories` will be cleaned up using `rag_service.delete_noisy_actions()`. Future tool calls will be routed to a dedicated `action_logs` table without vector embeddings.

> [!NOTE]
> **On-Device Flash Footprint**: The physical ESP32 `MEMORY.md` file will remain strictly capped (< 3 KB) to guarantee fast local prompt construction on Core 1 and prevent SRAM/PSRAM exhaustion.

---

## 3. Open Questions

All primary design decisions have been resolved through the `/grill-me` interview:
- **Decision 1**: Action logs decoupled from vector RAG (rolling 7-day TTL for raw actions; only real-world events promoted).
- **Decision 2**: 3-factor salience rubric (>30-day durability, actionability, user relevance) with automatic conflict resolution/superseding.
- **Decision 3**: Hierarchical tiering (bounded hot flash scratchpad + cold backend vector DB with periodic distillation).
- **Decision 4**: Context-aware RAG with explicit priming banner directly above the user message.
- **Decision 5**: Agent equipped with `memory_update` and `memory_forget` MCP tools + Web UI management.

No unresolved blockers remain.

---

## 4. Proposed Changes

### Component 1: Storage Layer & Action Log Decoupling (`espet-backend/services/`)

#### [MODIFY] `espet-backend/services/memory_db.py`
- Add a new table `action_logs`:
  ```sql
  CREATE TABLE IF NOT EXISTS action_logs (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      device_id TEXT NOT NULL,
      tool_name TEXT NOT NULL,
      arguments_json TEXT,
      result_summary TEXT,
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
  );
  CREATE INDEX IF NOT EXISTS idx_action_logs_dev_date ON action_logs(device_id, created_at);
  ```
- Add a cleanup method `purge_expired_actions(days=7)`.

#### [MODIFY] `espet-backend/services/rag_service.py`
- Update `delete_noisy_actions()` to purge all legacy `source_type='action'` records from `vec_memories`.
- Add `log_tool_action(device_id, tool_name, arguments_json, result_summary)` writing to `action_logs`.
- Implement `upsert_memory_with_conflict_resolution()`:
  - If a memory with similarity > 0.85 exists in the same category, update its content instead of creating a duplicate.
  - If a memory explicitly negates an older fact (e.g., preference change), mark or replace the previous entry.
- Implement `distill_working_memory(device_id, max_bytes=3000) -> str`:
  - Selects top user profile facts, enduring preferences, and active goals.
  - Formats them as clean markdown bullets for `/spiffs/memory/MEMORY.md`.

---

### Component 2: Autonomous Extraction & Salience Rubric (`espet-backend/services/`)

#### [MODIFY] `espet-backend/services/memory_organizer.py`
- Revise extraction system prompt with the **3-Factor Salience Rubric**:
  - **Durability**: Will this information still be true and useful 30+ days from now?
  - **Actionability**: Does this impact how ESPET serves the user, executes tasks, or behaves?
  - **User Relevance**: Is it tied to the user's identity, preferences, relationships, or commitments?
- Add structured handling for memory replacements/updates (`replaces_id` or `supersedes` in extraction JSON).
- Integrate conflict resolution before saving new memories.

---

### Component 3: Context-Aware Retrieval & Prompt Priming (`espet-backend/api/`)

#### [MODIFY] `espet-backend/api/cloud_router.py`
- Modify `_capture_episodic_action`:
  - Route general tool executions to `rag_service.log_tool_action(...)`.
  - Only promote high-salience external actions (e.g. `gmail_send_email`, `gcal_create_event`, `cron_add`) to `memory_chunks` under `source_type='event'`, written in natural language (e.g. "Sent email to John regarding FYP report").
- Enhance RAG search query generation:
  - If user message contains pronouns ("he", "she", "it", "they", "that", "his", "her"), extract recent nouns/entities from the previous turn to augment the vector search query.
- Format injected context with an attention-priming banner:
  ```markdown
  ## Established User Context (Priority: Follow Strictly)
  The following verified user facts and preferences must guide your response and tool arguments:
  - [preference] User prefers Celsius for temperature.
  - [fact] Dr. Wong Thein Lai is the FYP supervisor at TARUMT.
  ```

---

### Component 4: Agent Memory Management Tools (`espet-backend/api/`)

#### [MODIFY] `espet-backend/api/custom_tools.py`
- Add `execute_memory_update(arguments)`:
  - Updates an existing memory item by search query or ID.
- Add `execute_memory_forget(arguments)`:
  - Deletes a memory item matching a query or confirmation.
- Register `memory_update` and `memory_forget` in `CUSTOM_TOOLS` schema dictionary so ESP32 discovers them via `GET /api/tools`.

---

### Component 5: Documentation & Session Memory Rule (`.agent/`)

#### [MODIFY] `.agent/SYSTEM_OVERVIEW.md`
- Document the new 3-tier memory lifecycle, decoupled action log table, salience rubric, and distillation mechanism.
- Add recent change entry to the changelog.

---

## 5. Verification Plan

### Automated Tests
1. **Unit & Integration Tests (`espet-backend/tests/test_memory_system.py`)**:
   - `test_action_logging_not_in_vector_rag`: Verify tool calls write to `action_logs` and do not create rows in `vec_memories`.
   - `test_salience_rubric_filtering`: Verify transient chatter is rejected while enduring facts are extracted.
   - `test_memory_conflict_resolution`: Verify updated preferences replace older conflicting preferences.
   - `test_memory_distillation_budget`: Verify `distill_working_memory` generates output under 3,000 bytes.
   - `test_agent_memory_forget_and_update`: Verify `memory_update` and `memory_forget` custom tools work via MCP execution.
   ```bash
   cd espet-backend
   python -m pytest tests/test_rag_service.py tests/test_memory_system.py -v
   ```

2. **Firmware Compatibility Verification**:
   ```bash
   idf.py build
   ```

### Manual Verification
1. **Tool Invocation Audit**: Trigger a tool on the ESP32 (e.g. white LED or status check) and verify via SQLite / backend logs that `vec_memories` is NOT polluted with raw GPIO action records.
2. **Conversation Turn with Memory Priming**:
   - Tell ESPET: *"I am allergic to dairy products."*
   - Verify `MemoryOrganizer` extracts `[preference] User is allergic to dairy products.` into SQLite-Vec.
   - Next turn, ask: *"Can you suggest a breakfast for me?"*
   - Verify the LLM relay injects the dairy allergy into the primed context banner and the response strictly adheres to it.
3. **Agent Explicit Forget Tool**:
   - Tell ESPET: *"Forget that I am allergic to dairy."*
   - Verify the agent calls `memory_forget` and the chunk is removed from `vec_memories`.
