# Assistant Page Device Sync & Redesign Plan

## Context
The `#assistant` tab (`http://localhost:8000/#assistant`) manages the agent's identity, personality, profile, memories, schedules, and skills. Currently, configuration is stored globally on the backend without explicit device targeting, `MEMORY.md` and cron jobs are only pulled one-way by the ESP32 (on-device cron additions and memory writes are not pushed back), the personality editor takes up excessive page height, profile defaults do not match the required baseline, the SQLite-Vec RAG view retains an obsolete "Sync from MEMORY.md" button with hardcoded device ID `espet_s3_01`, and full-width explanatory banner boxes clutter each section. This plan specifies the full architecture and implementation for multi-device selection, true 2-way real-time synchronization between the physical ESPET device and backend, 5 personality presets with modal editing, updated profile defaults, scoped RAG search, and sleek title-case headers with info tooltips.

## Approach

### 1. Device Registry & Multi-Device Config Store
- **File**: `espet-backend/services/device_registry.py`
  - Expand `DeviceRegistry` to track all known devices in `data/devices.json`:
    - Record format: `{ "device_id": str, "name": str, "last_seen": str (ISO-8601), "online": bool, "ip": str, "fw": str }`.
    - Method `register_device(device_id: str, ip: str = "", fw: str = "") -> dict`: records or updates device, sets `online=True`, `last_seen=now()`, and persists.
    - Method `mark_offline(device_id: str)`: sets `online=False`, persists.
    - Method `list_devices() -> list[dict]`: returns all known devices sorted by `last_seen` descending. Auto-seeds `{ "device_id": "espet_s3_01", "name": "ESPET", "online": False, "last_seen": "2026-11-09T00:00:00Z", "ip": "127.0.0.1", "fw": "v5.5.4" }` if registry is empty.
    - Method `get_latest_device_id() -> str`: returns the device_id of the most recently active device (first in `list_devices()`).
- **File**: `espet-backend/config_store.py`
  - Refactor config loading to be per-device with fallback:
    - Helper `_device_config_path(device_id: str | None) -> Path`: returns `_DATA_DIR / f"config_{re.sub(r'[^a-zA-Z0-9_-]', '_', device_id)}.json"` if `device_id` is provided, else `_DATA_DIR / "config.json"`.
    - Update `_load_unlocked(device_id: str | None = None) -> dict`: if per-device file doesn't exist, copies initial state from `_CONFIG_FILE` or `_default_config()`.
    - Update `_save_unlocked(cfg: dict, device_id: str | None = None)`: writes atomically to the device's config file.
    - Update functions to accept `device_id: str | None = None`:
      - `get_config(device_id: str | None = None) -> dict`
      - `update_markdown(section: str, content: str, device_id: str | None = None) -> dict`
      - `add_job(payload: dict, device_id: str | None = None) -> tuple[dict, str | None]`
      - `update_job(job_id: str, payload: dict, device_id: str | None = None) -> tuple[dict, str | None]`
      - `delete_job(job_id: str, device_id: str | None = None) -> dict`
      - `update_device_settings(name: str, timezone_iana: str, sync_interval_s: int, device_id: str | None = None) -> dict`
    - Add `apply_device_report(device_id: str, memory_md: str | None, jobs: list | None) -> dict`:
      - Updates `cfg["memory_md"] = memory_md` if provided and non-empty.
      - Merges incoming device jobs into `cfg["jobs"]` without dropping web jobs.
      - Increments section versions if content changed and saves.
- **File**: `espet-backend/api/config_router.py`
  - Add endpoint `GET /api/devices`:
    - Returns `{ "devices": registry.list_devices(), "latest_device_id": registry.get_latest_device_id() }`.
  - Update `GET /api/config`:
    - Read `device_id` from query parameter `?device_id=...` or request header `X-Device-Id`. If missing, default to `registry.get_latest_device_id()`.
    - Return `store.device_bundle(store.get_config(device_id))`.
  - Update `PUT /api/ui/profile`, `PUT /api/ui/soul`, `PUT /api/ui/memory`, `PUT /api/ui/device`:
    - Accept `device_id` from query param or body. Pass to `store.update_markdown(...)`.
    - After update, trigger immediate push via `cloud_router.notify_device_sync(device_id)`.
  - Update `GET /api/ui/jobs`, `POST /api/ui/jobs`, `PUT /api/ui/jobs/{job_id}`, `DELETE /api/ui/jobs/{job_id}`:
    - Accept `device_id`, pass to `store.*_job(..., device_id=device_id)`.
    - After mutation, trigger `cloud_router.notify_device_sync(device_id)`.
  - Add endpoint `POST /api/config/sync-now`:
    - Query/body parameter: `device_id: str`.
    - Calls `cloud_router.notify_device_sync(device_id)`.
    - Returns `{ "ok": true, "device_id": device_id, "pushed": bool }`.

### 2. Physical ESPET 2-Way Real-Time Sync
- **File**: `espet-backend/api/cloud_router.py`
  - In `ws_device(websocket, key)`:
    - On `hello` frame: call `registry.register_device(device_id, ip=websocket.client.host, fw=hello.get("fw", ""))`.
    - In receive frame loop, add handlers for on-device state reports:
      - `ftype == "device_sync_report"`:
        - Extract `memory_md = frame.get("memory_md")`, `jobs = frame.get("jobs")`.
        - Call `store.apply_device_report(device_id, memory_md, jobs)`.
        - Log: `logger.info("Received device_sync_report from %s (memory_md %d bytes, %d jobs)", device_id, len(memory_md or ""), len(jobs or []))`.
      - `ftype == "device_memory_update"`:
        - Call `store.update_markdown("memory", frame.get("content", ""), device_id)`.
      - `ftype == "device_jobs_update"`:
        - Update device jobs in `store` for `device_id`.
    - In `finally` block: call `registry.mark_offline(device_id)`.
  - Add function `notify_device_sync(device_id: str) -> bool`:
    - Checks `registry.connections.get(device_id)`.
    - If connected, sends WS frame: `json.dumps({"type": "sync_now", "device_id": device_id})`. Returns `True`.
    - If offline, returns `False`.
- **File**: `main/channels/cloud_link/cloud_link.h` & `main/channels/cloud_link/cloud_link.c`
  - Export `esp_err_t cloud_link_send_sync_report(void)`:
    - Reads `/spiffs/memory/MEMORY.md` and `/spiffs/cron.json`.
    - Builds JSON: `{"type": "device_sync_report", "device_id": s_device_id, "memory_md": <content>, "jobs": <jobs_array>}`.
    - Sends over WebSocket via `send_json()`.
  - In `handle_frame`:
    - Add case `strcmp(type_str, "sync_now") == 0`:
      - Call `config_sync_trigger()`.
      - Log `ESP_LOGI(TAG, "Received sync_now from backend — triggered immediate config sync")`.
  - In `send_hello()`:
    - Follow `hello` with `cloud_link_send_sync_report()` to establish immediate initial alignment upon connection.
- **File**: `main/sync/config_sync.h` & `main/sync/config_sync.c`
  - Export `void config_sync_trigger(void)`:
    - Wakes `s_task` via `xTaskNotifyGive(s_task)` or unblocks wait so `sync_once()` executes immediately.
  - In `fetch_json`:
    - Derive STA MAC address and format `dev_id` (`ESP-XXXXXXXXXXXX`).
    - Append query parameter `?device_id=<dev_id>` and set header `esp_http_client_set_header(client, "X-Device-Id", dev_id)`.
- **File**: `main/tools/tool_files.c`
  - In `tool_write_file_execute` and `tool_edit_file_execute`:
    - If target `path` matches `MIMI_MEMORY_FILE`: call `cloud_link_send_sync_report()` immediately upon successful write.
- **File**: `main/tools/tool_cron.c`
  - In `tool_cron_add_execute` and `tool_cron_remove_execute`:
    - Upon successful job add/removal: call `cloud_link_send_sync_report()` to push the updated schedule to the backend.

### 3. Frontend App Store & Device Selector
- **File**: `espet-backend/src/api.ts`
  - Define interface:
    ```typescript
    export interface DeviceInfo {
      device_id: string;
      name?: string;
      online: boolean;
      last_seen: string;
      ip?: string;
      fw?: string;
      is_latest?: boolean;
    }
    ```
  - Add API client methods:
    - `devices: () => api<{ devices: DeviceInfo[]; latest_device_id: string }>("/api/devices")`
    - `syncNow: (device_id: string) => api<{ ok: boolean; pushed: boolean }>("/api/config/sync-now", json("POST", { device_id }))`
    - Update `config: (device_id?: string) => api<ConfigBundle>(`/api/config${device_id ? `?device_id=${device_id}` : ""}`)`
    - Update `saveProfile: (user_md: string, device_id?: string) => api(...)`
    - Update `saveSoul: (soul_md: string, device_id?: string) => api(...)`
    - Update `saveMemory: (memory_md: string, device_id?: string) => api(...)`
    - Update `createJob`, `updateJob`, `deleteJob` to pass `device_id`.
    - Update `listMemories` and `queryMemory` to require/pass `device_id`.
- **File**: `espet-backend/src/store.tsx`
  - Add state:
    - `selectedDeviceId: string` (persisted in `localStorage.getItem("espet.selected_device_id")`).
    - `devices: DeviceInfo[]`
    - `setSelectedDeviceId: (id: string) => void`
    - `refreshDevices: () => Promise<void>`
    - Update `loadConfig`: accepts optional `device_id`, defaults to `selectedDeviceId`.
    - On initial store mount: call `refreshDevices()`, resolve default device (`localStorage` value or `latest_device_id`), then load config for that device.
- **File**: `espet-backend/src/views/AssistantPage.tsx`
  - Add `<DeviceSyncBar />` card pinned at the top above `<PersonalityView />`:
    - Displays:
      - Title: "Target ESPET Device" with Title Case.
      - Info tooltip: "Select which physical ESPET device to synchronize with. All personality, profile, memory, and scheduled jobs in this tab directly mirror the physical flash of the selected device."
      - Select dropdown: renders each device with online badge (green = online, gray = offline), `device_id`, custom name, and "Latest" indicator.
      - "Sync Now" button with refresh icon: triggers `client.syncNow(selectedDeviceId)`, refetches config and memories, displays success toast.
      - Online/Offline status tag with last-seen timestamp.

### 4. Personality & Behavior (SOUL.md) Redesign
- **File**: `espet-backend/src/views/Personality.tsx`
  - Define the 5 behavior presets:
    1. **Default ESPET (Balanced Assistant)**: helpful, concise, "Stop Yapping", "Take Initiative" rules.
    2. **Robotic Pet (Playful Companion)**: friendly, cheerful, cute acoustic cues (*chirp*, *boop*), expressive companion tone.
    3. **Executive Butler (Professional)**: formal, discreet, flawless deference, zero casual banter.
    4. **Curious Scholar (Hardware Tutor)**: analytical, first-principles reasoning, electronics/STEM focused.
    5. **Minimalist Cyber (Ultra-Terse Action Engine)**: maximum information density, ≤2 sentences, pure action results.
  - UI Transformation:
    - Heading: `Personality & Behavior` (Title Case).
    - Info hover icon beside heading:
      `<Tooltip title="The first instruction the agent reads in every prompt — defines personality, values, and hard behavior rules. Strong constraints like 'Stop Yapping' and 'Take Initiative' visibly improve small model responses."><Icon name="info" size={15} style={{ color: "#a3a3a3", cursor: "pointer", marginLeft: 8 }} /></Tooltip>`
    - Replace the large embedded textarea on the main page with a list of the 5 presets:
      - Each item card shows: Preset Title, summary tag, active selection radio/badge, and "Preview & Edit" button.
      - Below preset list: shows active preset indicator (or "Custom" badge if user edited custom markdown).
    - Ant Design `Modal` (`open={modalOpen}`, `title="Preview & Edit SOUL.md"`, `width={760}`):
      - Contains `SourceField` markdown editor with live preview tabs and byte counter.
      - "Save to Device" button: calls `client.saveSoul(draft, selectedDeviceId)`, updates store, closes modal, triggers device push.
      - "Revert to Default" button: resets draft to selected preset template.
    - Remove the bottom `div className={styles.sectionNote}` callout banner completely.

### 5. User Profile (USER.md) Alignment
- **File**: `espet-backend/src/views/Profile.tsx`
  - Heading: `User Profile` (Title Case).
  - Info hover icon beside heading:
    `<Tooltip title="What the agent knows about you — preferred name, language, timezone, and background context. Saved content is synchronized directly to USER.md on the device."><Icon name="info" size={15} style={{ color: "#a3a3a3", cursor: "pointer", marginLeft: 8 }} /></Tooltip>`
  - Update defaults:
    - Name: `ESPET`
    - Language: `Chinese / English`
    - Timezone: Detect via `Intl.DateTimeFormat().resolvedOptions().timeZone`; if in `TIMEZONES` list, use it; otherwise fallback to `Asia/Kuala_Lumpur`.
    - Extra Context: `<empty>` (empty array `notes: []`).
  - "Reset to default" button sets:
    `{ name: "ESPET", language: "Chinese / English", timezone: detectedTz || "Asia/Kuala_Lumpur", notes: [] }`.
  - Remove bottom `div className={styles.sectionNote}` callout banner.
- **File**: `espet-backend/config_store.py`
  - Update `_DEFAULT_USER_MD`:
    ```markdown
    # User Profile

    - Name: ESPET
    - Language: Chinese / English
    - Timezone: Asia/Kuala_Lumpur
    ```

### 6. MEMORY.md & Long-Term Memory (SQLite-Vec RAG)
- **File**: `espet-backend/src/views/Memory.tsx`
  - **Tier 1 (On-Device Identity Summary)**:
    - Heading: `Identity & Profile Summary (MEMORY.md)` (Title Case).
    - Info hover icon beside heading:
      `<Tooltip title="High-level identity summary kept small (<2KB) for on-device SPIFFS sync. The agent reads this on boot to maintain identity continuity across reboots. Synchronized 2-way with the physical device."><Icon name="info" size={15} style={{ color: "#a3a3a3", cursor: "pointer", marginLeft: 8 }} /></Tooltip>`
    - Remove the top blue `<Alert type="info" message="On-Device Memory (MEMORY.md)" ... />` banner.
    - Keep full `MEMORY.md` markdown editor (`SourceField`), Quick Note append, Clear, and Save.
    - Save calls `client.saveMemory(draft, selectedDeviceId)` and triggers real-time device push.
  - **Tier 2 (Long-Term Vector Memory)**:
    - Heading: `Long-Term Memory (SQLite-Vec RAG)` (Title Case).
    - Info hover icon beside heading:
      `<Tooltip title="Permanent vector memory database powered by BAAI/bge-small-en-v1.5 and sqlite-vec. ESPET automatically writes here via autonomous tool interactions over time. Search tests vector similarity (<0.28 cosine distance) scoped to this device."><Icon name="info" size={15} style={{ color: "#a3a3a3", cursor: "pointer", marginLeft: 8 }} /></Tooltip>`
    - Remove the warning `<Alert type="warning" message="Long-Term Vector Memory Modification Warning" ... />` banner.
    - **Remove the "Sync from MEMORY.md" button** (`client.syncFromMemoryMd`) completely from the header bar.
    - Update `reloadMemories`: passes `{ device_id: selectedDeviceId, category: ... }`.
    - Sub-heading: `Semantic Vector Search Tester (NFR-5 Latency Compliance)` (Title Case).
    - Update `handleTestSearch`: calls `client.queryMemory(testQuery, 5, 0.28, selectedDeviceId)` — queries RAG memories strictly scoped to the active ESPET device, finding results immediately just like how the ESPET finds them during LLM relay.
    - Add/Edit modals pass `device_id: selectedDeviceId`.

### 7. Scheduled Jobs Alignment
- **File**: `espet-backend/src/views/Schedule.tsx`
  - Heading: `Scheduled Jobs` (Title Case).
  - Info hover icon beside heading:
    `<Tooltip title="Cron jobs running autonomously on the physical ESPET device. Synced 2-way between device and backend to ensure complete alignment. Device limits: 16 jobs, 31-char name, 255-char message."><Icon name="info" size={15} style={{ color: "#a3a3a3", cursor: "pointer", marginLeft: 8 }} /></Tooltip>`
  - Remove bottom `div className={styles.sectionNote}` callout banner.
  - Job creation, editing, and deletion pass `selectedDeviceId`.
  - When ESPET creates jobs on-device via `tool_cron_add`, they are reported via WebSocket and displayed in this list.

### 8. Skills Section
- **File**: `espet-backend/src/views/Skills.tsx`
  - Heading: `Skills` (Title Case).
  - Info hover icon beside heading:
    `<Tooltip title="Skills created here are written to /spiffs/skills/ on the device. A skill must start with a # Title heading — that's what the device parses. Each skill is a markdown playbook the agent can follow."><Icon name="info" size={15} style={{ color: "#a3a3a3", cursor: "pointer", marginLeft: 8 }} /></Tooltip>`
  - Remove bottom `div className={styles.sectionNote}` banner (the exact banner shown in Image #1).
  - Keep skill CRUD functionality unchanged.

### 9. Routes & Navigation Alignment
- **File**: `espet-backend/src/routes.ts`
  - Update `ASSISTANT` route metadata:
    - Personality: label `Personality & Behavior`, title `Personality & Behavior`.
    - Profile: label `User Profile`, title `User Profile`.
    - Schedule: label `Scheduled Jobs`, title `Scheduled Jobs`.

## Critical Files & Anchors
- `espet-backend/services/device_registry.py`: `DeviceRegistry` class, device listing, connection management, `data/devices.json` persistence.
- `espet-backend/config_store.py`: `_load_unlocked`, `_save_unlocked`, `apply_device_report`, per-device JSON files.
- `espet-backend/api/cloud_router.py`: `ws_device` frame loop, `device_sync_report` handling, `notify_device_sync`.
- `main/channels/cloud_link/cloud_link.c`: `cloud_link_send_sync_report`, `sync_now` frame handler.
- `espet-backend/src/views/AssistantPage.tsx`: Top `DeviceSyncBar`, multi-device context integration.
- `espet-backend/src/views/Personality.tsx`: 5 presets, modal editor, info tooltip, Title Case.
- `espet-backend/src/views/Memory.tsx`: Removal of "Sync from MEMORY.md", scoped RAG search, removal of Alert banners, info tooltips.

## Verification
1. **Device Registry & API Test**:
   - Run `pytest tests/test_cloud_gateway.py tests/test_rag_service.py` to ensure all existing backend tests pass.
   - Run curl test: `curl http://localhost:8000/api/devices` -> verifies JSON output with `devices` array and `latest_device_id`.
   - Run curl test: `curl "http://localhost:8000/api/config?device_id=ESP-TEST01"` -> verifies per-device config generation and default seeding.
2. **2-Way WebSocket & Config Sync Verification**:
   - Simulate device WS connection sending `hello` and `device_sync_report` with updated `memory_md` and `jobs`.
   - Verify `GET /api/config?device_id=...` reflects the device's reported memory and jobs.
   - Send `PUT /api/ui/memory` from web -> verify `sync_now` frame is pushed over the live WebSocket.
3. **Frontend Build & UI Smoke Test**:
   - Run `node tools/build-app.mjs` in `espet-backend` to ensure bundle compiles without errors.
   - Verify `#assistant` page in browser:
     - Target device picker appears at top with latest device selected by default.
     - Personality section displays 5 presets in a list with radio indicator and "Preview & Edit" button opening modal.
     - Profile defaults to Name: `ESPET`, Language: `Chinese / English`, Timezone: detected / `Asia/Kuala_Lumpur`, Extra Context: empty.
     - MEMORY.md displays full markdown with 2-way sync indicator.
     - Long-Term Memory (SQLite-Vec RAG) has no "Sync from MEMORY.md" button; semantic search tester returns matches scoped to selected device.
     - Scheduled jobs match device cron list with 2-way sync.
     - All section banners from Image #1 are removed and replaced with hoverable info tooltips beside Title Case headings.

## Assumptions & Contingencies
- If no ESPET device is physically powered on or connected, the system automatically uses `espet_s3_01` as the default placeholder device so the web interface is fully operational offline.
- If browser geolocation/timezone detection is unavailable or denied by security policies, timezone defaults strictly to `Asia/Kuala_Lumpur`.
- If a connected device disconnects while a user is saving, the backend persists the change locally with an incremented version counter; the physical device applies the update as soon as it reconnects.
