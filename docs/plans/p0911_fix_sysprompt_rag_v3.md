# System Prompt, RAG Memory & Profile Alignment Plan (v3 Final Architecture)

## Context
Analysis of `docs/plans/p0911_fix_sysprompt_rag.md` (v1) and `docs/plans/p0911_fix_sysprompt_rag_v2.md` (v2) against the live codebase identified critical flaws:
1. Stripping `context_builder.c` prompt without keeping a compact hardware pin section breaks physical device actions; `skills/gpio-control.md` is NOT included in prompt (only 1-line summary), and personality presets in `Personality.tsx` do not define pins.
2. `MEMORY.md` relabeled as "Identity Summary" collides directly with `USER.md` ("User Profile") and `SOUL.md` ("Personality & Behavior").
3. v2's dual-writer approach (`update_user_profile` tool + background `MemoryOrganizer`) causes duplicate patches, redundant `sync_now` frames, and SPIFFS flash wear.
4. Gating rule `< 4 words` drops standard user introductions ("im Cham Herman", "Call me Herman").
5. Runtime bugs in v1/v2: `assistant_text` and `tool_calls` undefined in `_llm_relay`, `distance` key missing in `rag_service.search` return dict, and unhandled Markdown JSON wrapping from local LLMs.

This plan details the unified, bug-free implementation for tooltips, working memory UI, noise-free RAG, profile auto-sync, and a compact, reliable firmware system prompt.

---

## Approach

### 1. Fix Tooltip Hover Interactions on Section Headers
- **File**: `espet-backend/src/components/Icon.tsx`
  - Refactor `Icon` using `React.forwardRef<SVGSVGElement, IconProps>`.
  - Extend `IconProps` from `React.SVGProps<SVGSVGElement>`. Forward `ref` and spread `...rest` onto `<svg>`.
- **Files**: `espet-backend/src/views/AssistantPage.tsx`, `Personality.tsx`, `Profile.tsx`, `Memory.tsx`, `Schedule.tsx`, `Skills.tsx`
  - Wrap tooltip trigger targets in `<span style={{ display: "inline-flex", alignItems: "center", cursor: "pointer" }}>`.

### 2. Working Memory (MEMORY.md) Clarification & Preview/Modal UI
- Clarify role: `MEMORY.md` is device scratchpad / working memory (active tasks, ongoing context), NOT "Identity Summary" (which belongs exclusively in `USER.md`).
- **File**: `espet-backend/config_store.py`
  - `_DEFAULT_MEMORY_MD`:
    ```markdown
    # Working Memory

    (empty - ESPET writes active task context and working notes here)
    ```
  - Fix bug in `_DEFAULT_USER_MD`: Change `- Name: ESPET` to `- Name: (not set)`.
- **File**: `espet-backend/src/views/Memory.tsx`
  - Label Tier 1 as `Working Memory (Device Scratchpad)`.
  - Tooltip: `"On-device working scratchpad (<2KB) for physical ESP32 flash sync. The agent uses this for active task context across reboots. User profile belongs in USER.md; long-term semantic memories are in SQLite-Vec RAG below."`
  - Replace inline `SourceField` with bounded scrollable read-only preview (`maxHeight: 220px`, monospace font, line/byte badges).
  - Add Modal Editor (`open={memoryModalOpen}`, `title="Edit Working Memory (MEMORY.md)"`, `width={760}`) with `SourceField`, "Clear", "Revert", and "Save to Device" buttons.
  - Retain "Quick note" append bar below preview card.

### 3. Noise Filtering in Episodic Action Capture
- **File**: `espet-backend/api/cloud_router.py`
  - In `_capture_episodic_action`:
    - Define `NOISE_TOOLS = {"get_current_time", "cron_list", "gpio_read", "read_file", "list_dir", "memory_search", "memory_save"}`.
    - If `name in NOISE_TOOLS`, skip adding episodic action memory. Only persist state-changing actions (`gpio_write`, `cron_add`, `cron_remove`, `gmail_send_email`, `create_event`).
- **File**: `espet-backend/services/rag_service.py`
  - Add method `delete_noisy_actions(device_id: str) -> int`:
    - Executes atomic delete on `vec_memories` and `memory_chunks` for `source_type = 'action'` records matching noise tool patterns.

### 4. Robust Profile Sync & Autonomous Turn Memory Organizer
- **File**: `espet-backend/config_store.py`
  - Implement Python helpers matching `Profile.tsx`:
    - `parse_user_md(md: str) -> dict`: extracts `name`, `language`, `timezone`, and `notes` list.
    - `build_user_md(fields: dict) -> str`: builds `# User Profile`, `- Name: ...`, `- Language: ...`, `- Timezone: ...`, and notes.
    - `patch_user_profile(device_id: str, name: str | None = None, language: str | None = None, timezone: str | None = None, note: str | None = None) -> bool`:
      - Compares against existing values; only bumps section version, writes file, and triggers sync if data actually changed.
- **File**: `espet-backend/services/memory_organizer.py`
  - Define `MemoryOrganizer`:
    - Robust response JSON parser that strips ````json ... ```` fences and extracts the outermost JSON block.
    - `extract_from_turn(device_id: str, user_text: str, assistant_json_or_text: str)`:
      - Safely parse assistant message content from raw relay payload without `NameError`.
      - Gating: Discard only if `user_text` is empty or pure punctuation; do NOT discard short strings (< 4 words) so introductions ("I'm Herman") are preserved. Skip if user message is solely a simple hardware command ("turn on led", "time").
      - Prompt server LLM for structured extraction:
        - `profile_update`: `{"name": "...", "language": "...", "timezone": "...", "note": "..."}` or null.
        - `memories`: list of `{"content": "...", "category": "fact"|"preference"|"event"|"note"}`.
      - If `profile_update` present and contains new data: call `config_store.patch_user_profile(device_id, ...)` and `cloud_router.notify_device_sync(device_id)`.
      - For each memory item:
        - Search `rag_service.search(query=content, device_id=device_id, top_k=1, threshold=0.15)`.
        - If `not res.get("results")`: call `rag_service.add_memory(device_id, content, source_type=category, source_ref="auto_organizer")`.
- **File**: `espet-backend/api/cloud_router.py`
  - In `_llm_relay`:
    - Background task: `asyncio.create_task(memory_organizer.extract_from_turn(device_id, user_query, resp.text))`.
- **File**: `espet-backend/api/memory_router.py`
  - Add `POST /api/memory/auto-organize?device_id=...` calling `rag_service.delete_noisy_actions(device_id)`.
- **File**: `espet-backend/src/views/Memory.tsx`
  - Add "AI Auto-Organize" button calling `autoOrganizeMemory()`.

### 5. Compact, Unbiased Firmware System Prompt (Safe Shrink)
- **File**: `main/agent/context_builder.c`
  - Replace the 73 lines of bloated, contradictory instructions with a compact, neutral bootstrap that PRESERVES critical hardware pin mappings and SPIFFS path rules:
    ```c
    off += snprintf(buf + off, size - off,
        "# ESPET\n\n"
        "You are ESPET, an embodied AI agent running on an ESP32-S3 microcontroller.\n"
        "Your behavior, tone, user context, and working memory are defined below.\n"
        "Use available tools to perceive the physical world and execute actions.\n\n"
        "## Hardware & Environment\n"
        "- White LED: GPIO 42 (use gpio_write: state 1=ON, 0=OFF)\n"
        "- Green Button: GPIO 41 (use gpio_read: state 0=Pressed, 1=Released)\n"
        "- File paths: all SPIFFS operations must start with /spiffs/\n"
        "- Current Time: call get_current_time when current date or time is needed\n\n");
    ```
  - Section headers:
    - `MIMI_SOUL_FILE` -> `## Personality & Behavior`
    - `MIMI_USER_FILE` -> `## User Profile`
    - `MIMI_MEMORY_FILE` -> `## Working Memory`
    - Recent notes -> `## Recent Notes`
    - Skills -> `## Available Skills`
- **File**: `main/tools/tool_registry.c`
  - Fix outdated pin description in `gpio_read`: Change `GPIO 39 = GREEN Button, GPIO 38 = WHITE Button` to `GPIO 41 = GREEN Button (GPIO 38/39 are shared I2S audio clocks)`.

---

## Critical Files & Anchors
- `espet-backend/src/components/Icon.tsx`: `forwardRef` and `SVGProps` forwarding.
- `espet-backend/src/views/Memory.tsx`: Working memory preview, modal editor, and Auto-Organize trigger.
- `espet-backend/config_store.py`: `_DEFAULT_USER_MD` name fix, `parse_user_md`, `build_user_md`, and `patch_user_profile`.
- `espet-backend/services/memory_organizer.py`: Robust JSON extractor, gating, and RAG deduplication.
- `espet-backend/services/rag_service.py`: `delete_noisy_actions` batch cleaner.
- `espet-backend/api/cloud_router.py`: `NOISE_TOOLS` filter in `_capture_episodic_action` and safe `_llm_relay` task invocation.
- `main/agent/context_builder.c`: Neutral prompt bootstrap retaining essential hardware and path invariants.
- `main/tools/tool_registry.c`: Corrected `gpio_read` tool pin documentation.

---

## Verification
1. **Frontend Tooltips**: Hover over all `#assistant` section info icons; verify Ant Design tooltips display text without console warnings.
2. **Working Memory UI**: Verify `#assistant` renders Tier 1 as a compact preview container; modal editor allows full edits and pushes updates to device.
3. **Noise Action Filtering**: Trigger `get_current_time` and `cron_list` tool calls; verify no `source_type="action"` records are inserted into SQLite-Vec. Verify `POST /api/memory/auto-organize` cleans existing read-only tool logs.
4. **User Profile Auto-Sync**: Send user message `"im Cham Herman, still finding internship position"`. Verify `USER.md` updates Name to "Cham Herman" and adds note without duplicate triggers or flash spam.
5. **Hardware Execution with Compact Prompt**: Instruct ESPET to turn on/off the white LED. Verify `gpio_write` with pin 42 executes successfully without hallucination or pin queries.
