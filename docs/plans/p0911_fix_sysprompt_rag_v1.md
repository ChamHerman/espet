# Assistant Memory System & System Prompt Alignment Plan

## Context
Recent testing of the `#assistant` tab (`http://localhost:8000/#assistant`) and physical ESPET interactions revealed four core usability and architecture issues:
1. Section header info tooltip icons (`<Tooltip><Icon name="info" .../></Tooltip>`) do not display text on hover because `Icon` drops event handlers and lacks `forwardRef`.
2. `MEMORY.md` is currently misidentified as "Long-term Memory" (competing with SQLite-Vec RAG), and its inline textarea in Tier 1 consumes excessive vertical space on the main page instead of defaulting to a clean preview with a dedicated modal editor.
3. SQLite-Vec RAG memory only contains raw action logs (`Action: executed tool ...` with `source_type="action"`), heavily spammed by read-only tools like `get_current_time` and `cron_list`. High-level categories (`fact`, `preference`, `event`, `note`) can only be keyed in manually via Web UI, leaving the agent without autonomous memory organization.
4. ESPET's compiled-in system prompt in `main/agent/context_builder.c` is bloated (~73 lines of hardcoded instructions, pin mappings, and contradictory personality defaults like "Be helpful, accurate, and concise") that bias the model and conflict with dynamic personality presets (`SOUL.md`), user profile (`USER.md`), and skills.

This plan specifies the architecture and implementation to fix tooltips, reclassify `MEMORY.md` as working memory with preview + modal editing, implement an autonomous AI memory organizer for RAG, and shrink ESPET's system prompt to a neutral, basic bootstrap.

---

## Approach

### 1. Fix Tooltip Hover Interactions on Section Headers
- **File**: `espet-backend/src/components/Icon.tsx`
  - Refactor `Icon` to use `React.forwardRef<SVGSVGElement, IconProps>`.
  - Extend `IconProps` from `React.SVGProps<SVGSVGElement>` so Ant Design's `Tooltip` (via `rc-trigger` / `rc-tooltip` `cloneElement`) can pass `onMouseEnter`, `onMouseLeave`, `onPointerEnter`, `onFocus`, `onBlur`, `ref`, and `aria-*` attributes.
  - Forward `ref` and spread `...rest` onto the root `<svg>` element.
- **Files**: `espet-backend/src/views/AssistantPage.tsx`, `Personality.tsx`, `Profile.tsx`, `Memory.tsx`, `Schedule.tsx`, `Skills.tsx`
  - Wrap tooltip trigger targets in `<span style={{ display: "inline-flex", alignItems: "center", cursor: "pointer" }}>` to guarantee event delegation and focus behavior across all browser environments.

### 2. Working Memory (MEMORY.md) Reclassification & Preview/Modal UI
- **File**: `espet-backend/config_store.py`
  - Update `_DEFAULT_MEMORY_MD`:
    ```markdown
    # Identity & Profile Summary

    (empty - ESPET summarizes ongoing identity and profile context here)
    ```
- **File**: `espet-backend/src/views/Memory.tsx`
  - Update `EMPTY_MEMORY`: `# Identity & Profile Summary\n\n(empty - ESPET summarizes ongoing identity and profile context here)\n`.
  - Re-label Tier 1 Heading to `Working Memory (Identity & Profile Summary)`.
  - Update Tooltip: `"On-device working memory and identity summary kept small (<2KB) for physical ESP32 flash sync. The agent reads this on boot to maintain identity continuity across reboots. Long-term semantic facts and episodic memories are persisted in SQLite-Vec RAG below."`
  - Replace inline `SourceField` on the main card with a styled read-only preview container:
    - Bounded scrollable markdown text block (`maxHeight: 220px`, monospaced font, subtle background `rgba(255, 255, 255, 0.02)`).
    - Footer row showing byte count, line count badge, and "Edit MEMORY.md" button with pencil icon.
    - Keep "Quick note" append input directly below the preview card for fast single-line additions without opening the modal.
  - Add Modal Editor (`open={memoryModalOpen}`, `title="Edit Working Memory (MEMORY.md)"`, `width={760}`):
    - Contains full `SourceField` editor with draft binding.
    - Footer buttons:
      - Danger button: "Clear MEMORY.md" (clears draft to `EMPTY_MEMORY`).
      - Button: "Revert" (reverts draft to server `config.memory_md`).
      - Button: "Cancel" (closes modal).
      - Primary Button: "Save to Device" (calls `client.saveMemory(draft, selectedDeviceId)` and triggers real-time WebSocket push).

### 3. Autonomous AI Memory Organization & SQLite-Vec Noise Filtering
- **File**: `espet-backend/services/memory_organizer.py` (new service)
  - Define `MemoryOrganizer` class:
    - `extract_from_turn(device_id: str, user_text: str, assistant_text: str, tool_calls: list[dict] | None = None) -> list[dict]`:
      - Gating: If `user_text` is trivial (e.g. under 4 words, simple hardware commands like "turn on led", "check button", "time", or greetings like "hi", "ping"), return `[]` immediately without consuming LLM inference.
      - Extraction: Calls server-configured LLM (`gateway_settings.llm_config()`) via `chat/completions` with low temperature (0.1) and strict extraction prompt:
        ```
        Extract enduring user knowledge, preferences, personal events, and critical notes from this dialogue turn.
        Categories:
        - "preference": user likes/dislikes, communication habits, device preferences
        - "fact": personal profile info, project details, deadlines, credentials, background facts
        - "event": scheduled appointments, consultations, deadlines, meetings
        - "note": specific instructions or reminders to remember
        Do not extract transient greetings, conversational pleasantries, or low-level tool logs.
        If no enduring memory is found, output: []
        Output strictly a JSON list:
        [{"content": "<concise declarative memory sentence>", "category": "fact"|"preference"|"event"|"note"}]
        ```
      - Deduplication: For each extracted item, queries `rag_service.search(query=content, device_id=device_id, top_k=1, threshold=0.15)`.
      - Indexing: If distance > 0.15 (no existing near-duplicate), calls `rag_service.add_memory(device_id=device_id, content=content, source_type=category, source_ref="auto_organizer")`.
    - `auto_organize_device(device_id: str) -> dict`:
      - Cleans noisy action logs: Identifies `source_type="action"` records whose content starts with `Action: executed tool get_current_time` or `Action: executed tool cron_list` or `Action: executed tool memory_search` or `Action: executed tool memory_save` and deletes them from `vec_memories` and `memory_chunks`.
      - Scans remaining high-signal actions (e.g. `cron_add`) and unindexed bullets from `USER.md`/`MEMORY.md` to classify into `fact`/`preference`/`event`/`note`.
      - Returns `{"ok": True, "cleaned_noise": cleaned_count, "extracted_count": extracted_count, "items": [...]}`.
- **File**: `espet-backend/api/cloud_router.py`
  - In `_capture_episodic_action`:
    - Add filter `NOISE_TOOLS = {"get_current_time", "cron_list", "gpio_read", "read_file", "list_dir", "memory_search", "memory_save"}`.
    - If `name in NOISE_TOOLS`, skip adding episodic action memory. Only state-changing actions (e.g. `cron_add`, `cron_remove`, `gpio_write`, `gmail_send_email`, `create_event`) are recorded as actions.
  - In `_llm_relay`:
    - After 200 OK response, launch asynchronous task:
      `asyncio.create_task(memory_organizer.extract_from_turn(device_id, user_query, assistant_text, tool_calls))`.
      Runs completely in the background without adding latency to the live WebSocket response.
- **File**: `espet-backend/api/memory_router.py`
  - Add endpoint `POST /api/memory/auto-organize`:
    - Parameter: `device_id: str = Query(...)`.
    - Calls `await asyncio.to_thread(memory_organizer.auto_organize_device, device_id)`.
    - Returns `{"ok": True, "cleaned_noise": int, "extracted_count": int, "items": list}`.
- **File**: `espet-backend/src/api.ts`
  - Add `autoOrganizeMemory: (device_id: string = "espet_s3_01") => api<{ ok: boolean; cleaned_noise: number; extracted_count: number }>("/api/memory/auto-organize?device_id=" + encodeURIComponent(device_id), json("POST", {}))`.
- **File**: `espet-backend/src/views/Memory.tsx`
  - In Tier 2 (Long-Term Vector Memory) header:
    - Add button next to "+ Add memory":
      `<Button icon={<Icon name="sparkles" size={14} />} loading={organizing} onClick={handleAutoOrganize}>AI Auto-Organize</Button>`.
    - Shows spinning state while organizing, displays toast on completion: `"Auto-organized: cleaned ${res.cleaned_noise} noise log(s), added ${res.extracted_count} memory item(s)"`, and calls `reloadMemories()`.

### 4. Shrink ESPET's System Prompt on Firmware
- **File**: `main/agent/context_builder.c`
  - Strip the 73 lines of hardcoded instructions, pin mappings, and contradictory personality rules from `context_build_system_prompt()`.
  - Replace initial buffer with a clean, unbiased, neutral bootstrap:
    ```c
    off += snprintf(buf + off, size - off,
        "# ESPET\n\n"
        "You are ESPET, an embodied AI agent running on an ESP32-S3 microcontroller.\n"
        "Your behavior, tone, user context, and working memory are defined in the sections and files below.\n"
        "Use available tools to perceive the physical world and execute actions.\n");
    ```
  - Update dynamic section titles to match backend naming:
    - `MIMI_SOUL_FILE` -> `## Personality & Behavior`
    - `MIMI_USER_FILE` -> `## User Profile`
    - `MIMI_MEMORY_FILE` -> `## Working Memory (Identity Summary)`
    - Recent daily notes -> `## Recent Notes`
    - Skills -> `## Available Skills`
  - Leave tone/persona completely to `SOUL.md`, user context to `USER.md`, identity summary to `MEMORY.md`, hardware pin rules to `skills/gpio-control.md`, and long-term memory to SQLite-Vec RAG.

---

## Critical Files & Anchors
- `espet-backend/src/components/Icon.tsx`: `forwardRef` and `...rest` prop forwarding for SVG icon.
- `espet-backend/src/views/Memory.tsx`: Preview mode for Working Memory, `Modal` editor, "AI Auto-Organize" button, Title Case headings.
- `espet-backend/services/memory_organizer.py`: Autonomous turn memory extraction and on-demand noise cleanup/categorization.
- `espet-backend/api/cloud_router.py`: Filter noisy inspection tools in `_capture_episodic_action`; trigger background turn memory organizer in `_llm_relay`.
- `main/agent/context_builder.c`: Stripped neutral system prompt bootstrap and section header alignment.

---

## Verification
1. **Tooltip Verification**:
   - Run `node tools/build-app.mjs` in `espet-backend`.
   - Hover over each section info icon on `#assistant` tab (`Target ESPET Device`, `Personality & Behavior`, `User Profile`, `Working Memory`, `Long-Term Memory`, `Scheduled Jobs`, `Skills`) -> Tooltip popover appears reliably on hover with informative text.
2. **Working Memory Preview & Modal Verification**:
   - Inspect `#assistant` page: Tier 1 renders as a compact, read-only preview box showing `MEMORY.md` content and byte/line badges.
   - Click "Edit MEMORY.md" -> Modal opens with `SourceField` editor.
   - Edit text and click "Save to Device" -> Closes modal, pushes `sync_now` frame, updates preview on card.
   - Quick note input on card appends note immediately and updates preview.
3. **AI Memory Organizer & Noise Filter Verification**:
   - Unit test `test_memory_organizer.py`:
     - Test that calling `extract_from_turn` with a preference ("I like black coffee without sugar") adds a `source_type="preference"` entry into SQLite-Vec.
     - Test that calling `extract_from_turn` with a fact ("FYP deadline is on Nov 24") adds a `source_type="fact"` entry.
     - Test that calling `_capture_episodic_action` with `get_current_time` or `cron_list` is rejected as noise and not inserted into RAG.
     - Test `POST /api/memory/auto-organize` cleans existing `get_current_time` actions and leaves high-value memories intact.
   - Click "AI Auto-Organize" in Web UI: Noisy records cleaned, category filters (`Fact`, `Preference`, `Event`, `Note`) populated.
4. **Firmware System Prompt Verification**:
   - Verify `context_build_system_prompt()` builds a concise prompt without pin hardcoding or conflicting personality directives.
   - Run firmware syntax check via ESP-IDF build or compiler check if available.

---

## Assumptions & Contingencies
- If the backend LLM is temporarily unreachable or offline during a turn, `extract_from_turn` logs a debug warning and fails silently in the background without disrupting the device's conversation loop.
- If no memory facts or preferences are detected in a turn (e.g. conversational small talk), the extractor returns `[]` and writes nothing to the database.
- Noise cleanup only targets automated read-only tools (`get_current_time`, `cron_list`, `memory_search`, `memory_save`); user-created custom action memories remain untouched.
