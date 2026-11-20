# BLE Provisioning Backend Server URL Implementation Plan

- **Goal**: Allow users to optionally configure the backend server URL via the Web Bluetooth provisioning interface (`docs/ble_prov.html`) and persist it on the ESP32 via a custom BLE endpoint.
- **Architecture**: Web Bluetooth (`esp-idf-provisioning-web`) sends custom JSON payload to ESP-IDF `wifi_prov_mgr` custom endpoint `custom-config` before Wi-Fi credentials; ESP32 saves to NVS and uses it in `backend_discovery.c`.
- **Tech Stack**: HTML/JavaScript (Web Bluetooth), C (ESP-IDF v5.5+, FreeRTOS, NVS, cJSON).

---

## Proposed Changes

### Task 1: Web Interface Field & Logic in `docs/ble_prov.html`
- Add input field `#backend_url` in Step 2.
- In `#btn-provision` click handler, format and validate URL.
- Send payload via `provDevice.sendData('custom-config', ...)` if provided.
- Log action in the UI event log.

### Task 2: ESP32 Configuration Key in `main/mimi_config.h`
- Add `#define MIMI_NVS_BACKEND "backend_cfg"` and `#define MIMI_NVS_KEY_BACKEND_URL "url"`.

### Task 3: Custom BLE Endpoint in `main/onboard/wifi_onboard_ble.c`
- Create endpoint `wifi_prov_mgr_endpoint_create("custom-config")` before starting provisioning.
- Register handler `wifi_prov_mgr_endpoint_register("custom-config", custom_config_prov_handler, NULL)` after starting provisioning.
- Parse JSON, extract `"backend_url"`, validate, and write to NVS.

### Task 4: NVS Integration in `main/sync/backend_discovery.c`
- Update `backend_discovery_base_url()` to check NVS for configured URL before falling back to `MIMI_BACKEND_URL`.
- Update `discovery_task()` to utilize NVS URL when mDNS fails or prioritize if available.

### Task 5: System Overview & Build Verification
- Update `.agent/SYSTEM_OVERVIEW.md` as required by repo rules.
- Run `python $IDF_PATH/tools/idf.py build` using the ESP-IDF v5.5.4 environment.

---

## Verification Plan
- Build firmware cleanly without warnings or errors.
- Verify `docs/ble_prov.html` syntax and UI layout.
- Check diffs in `.agent/SYSTEM_OVERVIEW.md`.
