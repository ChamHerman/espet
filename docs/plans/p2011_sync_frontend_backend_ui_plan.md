# Implementation Plan: Synchronize Frontend Web UI with Backend and ESPET Device

Ensure bidirectional real-time synchronization between the ESPET microcontroller, FastAPI backend server, and the React Web Dashboard (`http://localhost:8000/#assistant`). When settings or personalizations are modified via voice, chat, or the web dashboard, all three components immediately reflect the identical configuration.

---

## Problem & Root Cause Analysis

1. **Target Device ID Mismatch**:
   - When the agent executes self-modification tools from the ESP32 (`/api/mcp/execute`), the firmware omitted the `X-Device-Id` header.
   - The backend defaulted `device_id` to `registry.get_latest_device_id()`.
   - `registry.get_latest_device_id()` sorted by `last_seen` timestamp only, mistakenly targeting an offline mock test device (`ESP-TIME01`) instead of the active connected hardware device (`ESP-14C19F51F1EC`).
   - Consequently, voice changes were saved to `config_ESP-TIME01.json`, while the frontend and physical device read `config_ESP-14C19F51F1EC.json`.

2. **Preset Content Mismatch**:
   - `PERSONALITY_PRESETS` in `custom_tools.py` only contained 4 presets (missing `minimalist`), and `scholar` had subtle text differences compared to `Personality.tsx`.
   - In `Personality.tsx`, `matchingPreset` compares `canonical(preset.content) === canonical(config.soul_md)`. Because the applied preset string did not match byte-for-byte, the active preset card was not highlighted and the radio button fell back to the previous/default state.

3. **Frontend Lacks Reactive State & Polling**:
   - `store.tsx` loaded `config` once on application mount and never polled `/api/config/version` or listened to WebSocket config notifications.
   - Component drafts in `Profile.tsx`, `Memory.tsx`, and `Skills.tsx` were initialized once via `useState` and did not re-sync when `config` in the global store was updated.

---

## User Review Required

> [!IMPORTANT]
> - **Real-Time Polling & WebSocket Broadcast**: The Web UI will poll the lightweight `/api/config/version` endpoint every 2.5 seconds (and immediately upon tab focus), and also listen for WebSocket `config_updated` broadcasts from `notify_device_sync()`. This guarantees sub-second UI updates when voice commands are spoken.
> - **Connected Device Prioritization**: The backend registry will strictly prioritize active online devices with open WebSockets (`did in self.connections`) over stale offline test records.

---

## Proposed Changes

### Component 1: Firmware Hardware Identification (`main/tools/tool_registry.c`)

#### [MODIFY] `main/tools/tool_registry.c`
- When invoking `tool_remote_execute()` (`POST /api/mcp/execute`), read the device Wi-Fi STA MAC address (`ESP-%02X%02X...`) and send it as the `X-Device-Id` HTTP header.
- Guarantees the backend always knows the exact physical device executing the tool.

```c
// In tool_remote_execute:
uint8_t mac[6] = {0};
esp_read_mac(mac, ESP_MAC_WIFI_STA);
char dev_id[32];
snprintf(dev_id, sizeof(dev_id), "ESP-%02X%02X%02X%02X%02X%02X",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
esp_http_client_set_header(client, "X-Device-Id", dev_id);
```

---

### Component 2: Backend MCP Router & Device Registry

#### [MODIFY] `espet-backend/api/mcp_router.py`
- Accept `x_device_id: str | None = Header(None, alias="X-Device-Id")` in `execute_tool`.
- Automatically inject `device_id` into `request.arguments` so all tools operate on the correct device configuration file.

#### [MODIFY] `espet-backend/services/device_registry.py`
- In `get_latest_device_id()`: prioritize online connected devices (`item.get("online")` / `did in self.connections`) before falling back to newest `last_seen`.

#### [MODIFY] `espet-backend/api/cloud_router.py`
- In `notify_device_sync(device_id)`: broadcast a `{"type": "config_updated", "device_id": device_id, "versions": ...}` message to all active browser WebSockets (`_web_chat_clients`).

---

### Component 3: Preset Alignment in Backend Tools (`espet-backend/api/custom_tools.py`)

#### [MODIFY] `espet-backend/api/custom_tools.py`
- Update `PERSONALITY_PRESETS` to include all 5 presets (`default`, `robotic_pet`, `butler`, `scholar`, `minimalist`) matching the exact markdown in `Personality.tsx` byte-for-byte.

---

### Component 4: Frontend Reactive State & Real-Time Sync (`espet-backend/src/`)

#### [MODIFY] `espet-backend/src/store.tsx`
- Add lightweight version polling hook:
  - Calls `client.configVersion(selectedDeviceId)` every 2500ms.
  - If `versions` in response has any counter greater than `config.versions`, calls `loadConfig(selectedDeviceId)`.
  - Listen to window `focus` event to refresh stale data immediately when returning to the tab.

#### [MODIFY] `espet-backend/src/views/Personality.tsx`
- Ensure `selectedPresetId` and `draft` sync reactively when `config.soul_md` updates in the store.
- Fix radio button checked logic: `checked={isCurrentActive}` so the radio strictly mirrors the active configuration on device.

#### [MODIFY] `espet-backend/src/views/Profile.tsx`
- Add `useEffect` that updates `draft` and `raw` whenever `config.user_md` changes (when not dirty).

#### [MODIFY] `espet-backend/src/views/Memory.tsx`
- Add `useEffect` updating `draft` when `config.memory_md` changes, and re-fetching vector memories when `config.versions.memory` bumps.

#### [MODIFY] `espet-backend/src/views/Skills.tsx`
- Add `useEffect` reloading skills when `config.versions.skills` bumps.

#### [BUILD] `npm run build:app`
- Compile TypeScript changes in `src/` to `static/vendor/app/` with cache-busting stamp.

---

## Verification Plan

### Automated Verification
1. **Backend & Device Targeting Test**:
   - Run a Python test script simulating tool execution with `X-Device-Id: ESP-14C19F51F1EC`.
   - Verify `config_ESP-14C19F51F1EC.json` is updated and `config_store.get_config("ESP-14C19F51F1EC")` returns the new preset.
2. **Frontend Build Verification**:
   - Run `npm run build:app` in `espet-backend/` to confirm clean compilation.
3. **Firmware Build Verification**:
   - Run `idf.py build` using ESP-IDF to verify `X-Device-Id` addition compiles cleanly.

### Manual Verification
1. Open `http://localhost:8000/#assistant` in browser.
2. Speak or send chat command: *"Hey ESPET, switch personality to Robotic Pet"*.
3. Verify that within ~1-2 seconds, the `Personality & Behavior` card on `http://localhost:8000/#assistant` automatically highlights **Robotic Pet (Playful Companion)** with green tag `Active On Device` without manually refreshing the browser.
4. Speak: *"My name is Herman"*. Verify **User Profile** card updates `Name: Herman` automatically.
5. Apply a preset from the Web UI (e.g. click **Apply** on Executive Butler) and verify the physical ESPET logs `Received sync_now from backend` and updates its `/spiffs/config/SOUL.md`.
