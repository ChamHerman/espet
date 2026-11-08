# Sync Bound Telegram Chat ID to ESPET & Fix Outbound Messages

## Context
When ESPET detects a wake word, it attempts to notify the user on Telegram via `telegram_send_to_last_chat()`. However, the device currently drops the message with:
`W (...) telegram: Skipping Telegram send for button event: no active Telegram chat ID yet`.

The device operates in Hybrid Cloud Mode (`MIMI_CLOUD_LINK_ENABLE=1`). The Telegram bot token and the device binding (`-5358063159` ↔ `ESP-14C19F51F1EC`) exist only on the backend companion server (`espet-backend`). The backend currently never informs the ESP32 of its bound chat ID, and `telegram_send_to_last_chat()` on the ESP32 only works in standalone mode using a local bot token (which is empty in hybrid mode). This plan syncs the bound chat ID from backend to ESP32 on connection/binding, persists it in NVS, and routes outbound messages through `cloud_link_send_text()`.

## Approach

### 1. Backend: Expose and Push Bound Chat ID (`espet-backend/`)
- In `services/device_registry.py`:
  - Add helper `primary_chat_for_device(self, device_id: str) -> str | None`: iterates `self.chat_to_device` under `self._lock` and returns the first `chat_id` bound to `device_id`.
  - Add helper `chats_for_device(self, device_id: str) -> list[str]`: returns all `chat_id`s bound to `device_id`.
- In `api/cloud_router.py`:
  - In `device_endpoint` (`/ws/device`), during the `hello` handshake:
    Look up `primary_chat = registry.primary_chat_for_device(device_id) or ""`.
    Include `"chat_id": primary_chat` in the `hello_ack` response payload.
  - In `bind_chat` (`POST /api/cloud/bind`):
    After `registry.bind(str(cid), str(did))`, check if `did` is currently connected (`registry.connections.get(str(did))`). If connected, push `{"type": "bind_sync", "chat_id": str(cid), "device_id": str(did)}` to update the device in real time.
- In `services/telegram_gateway.py`:
  - When `auto_bind_single_device(chat_id)` binds a chat ID to a single connected device, push `{"type": "bind_sync", "chat_id": str(chat_id), "device_id": str(device_id)}` to that device's WebSocket.

### 2. ESP32 Firmware: Store Bound Chat ID in `cloud_link` (`main/channels/cloud_link/`)
- In `channels/cloud_link/cloud_link.h`:
  - Declare `const char *cloud_link_get_bound_chat_id(void);`
  - Declare `void cloud_link_set_bound_chat_id(const char *chat_id);`
- In `channels/cloud_link/cloud_link.c`:
  - Define static buffer `static char s_bound_chat_id[64] = {0};`
  - In `cloud_link_start()`, initialize `s_bound_chat_id` from NVS (`MIMI_NVS_TG`, `TG_LAST_CHAT_NVS_KEY`).
  - In `cloud_link_set_bound_chat_id(const char *chat_id)`:
    - Copy to `s_bound_chat_id`.
    - Persist to NVS (`MIMI_NVS_TG`, `TG_LAST_CHAT_NVS_KEY`).
    - Sync to `telegram_set_last_chat_id(chat_id)` in `telegram_bot.c`.
  - In `handle_frame()`:
    - For `hello_ack`: extract `"chat_id"`. If string and non-empty, call `cloud_link_set_bound_chat_id()`.
    - Add case `bind_sync`: extract `"chat_id"`. If string and non-empty, call `cloud_link_set_bound_chat_id()`.
    - In `message_in`: extract `"chat_id"`. If string and non-empty, call `cloud_link_set_bound_chat_id()`.

### 3. ESP32 Firmware: Route `telegram_send_to_last_chat` via `cloud_link` (`main/channels/telegram/`)
- In `channels/telegram/telegram_bot.h`:
  - Declare `void telegram_set_last_chat_id(const char *chat_id);`
  - Declare `const char *telegram_get_last_chat_id(void);`
- In `channels/telegram/telegram_bot.c`:
  - Implement `telegram_set_last_chat_id()` and `telegram_get_last_chat_id()`.
  - Update `telegram_send_to_last_chat(const char *text)`:
    - If `MIMI_CLOUD_LINK_ENABLE` is active and `cloud_link_is_connected()`:
      Retrieve bound chat ID via `cloud_link_get_bound_chat_id()`, fallback to `s_last_chat_id`.
      If valid non-empty chat ID exists, call `cloud_link_send_text(cid, text)`.
    - If standalone mode (or cloud link unavailable), fallback to existing `telegram_send_message(s_last_chat_id, text)`.
    - If no chat ID exists in either mode, log:
      `W (...) telegram: Skipping Telegram send: no active or bound Telegram chat ID yet`.

### 4. ESP32 Firmware: Wake Notification Check (`main/voice/voice_client.c`)
- In `voice_client.c` where wake word matches:
  - Call `telegram_send_to_last_chat("I heard you dy! Mic confirmed working.")`.
  - Log result and active chat ID for verification.

## Critical Files & Anchors
1. `espet-backend/services/device_registry.py` (lines 110-140): Add `primary_chat_for_device` and `chats_for_device`.
2. `espet-backend/api/cloud_router.py` (lines 100-115, 350-365): Add `chat_id` to `hello_ack` and send `bind_sync` on `/bind`.
3. `main/channels/cloud_link/cloud_link.c` (lines 115-140, 205-215, 385-400): Handle `chat_id` in `hello_ack`, `bind_sync`, and `message_in`; manage `s_bound_chat_id`.
4. `main/channels/telegram/telegram_bot.c` (lines 640-655): Make `telegram_send_to_last_chat()` hybrid-aware using `cloud_link_send_text()`.
5. `main/voice/voice_client.c` (lines 840-850): Wake word match notification trigger.

## Verification
1. **Backend Unit Tests**:
   - Run `pytest espet-backend/tests/test_cloud_gateway.py`.
   - Add new test case `test_tc12_hello_ack_includes_bound_chat()` asserting `hello_ack` contains `"chat_id": "-200"` when device is bound.
2. **Firmware Compilation**:
   - Run Windows ESP-IDF compile command:
     ```cmd
     cmd.exe /c "set IDF_TOOLS_PATH=C:\Espressif\tools&& set IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v5.5.4\venv&& set IDF_PATH=C:\esp\v5.5.4\esp-idf&& set PYTHONIOENCODING=utf-8&& set PYTHONUTF8=1&& C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe C:\esp\v5.5.4\esp-idf\tools\idf.py build"
     ```
   - Must build with zero errors.
3. **End-to-End Live Check**:
   - Flash firmware to ESP32: `idf.py -p COM3 flash monitor`.
   - On boot, observe log: `cloud gateway accepted this device; bound chat_id from gateway: -5358063159`.
   - Trigger mic wake word ("Hi expert" / "Hi ESP").
   - Observe log: `Device reply -> Telegram chat -5358063159: I heard you dy! Mic confirmed working.`
   - Confirm Telegram app receives the message in the bound group/chat.

## Assumptions & Contingencies
- **Assumptions**: The backend dashboard binding `-5358063159` ↔ `ESP-14C19F51F1EC` is already saved in `espet-backend/data/cloud_bindings.json`.
- **Contingency**: If the device boots when backend is offline or before WiFi connects, `s_bound_chat_id` is restored from local NVS (`TG_LAST_CHAT_NVS_KEY`). Once backend reconnects, `hello_ack` refreshes the value.
