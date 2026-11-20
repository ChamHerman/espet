# Design Spec: BLE Provisioning Backend Server URL Configuration

- **Date**: 2026-11-19
- **Topic**: BLE Provisioning Backend URL Field (`docs/ble_prov.html` + ESP32 Firmware)
- **Status**: Approved

---

## 1. Context & Motivation

ESPET supports Web Bluetooth provisioning via `docs/ble_prov.html` to configure Wi-Fi credentials without requiring a mobile app installation. In scenarios such as mobile hotspots (e.g. iPhone Personal Hotspot), client-to-client multicast/mDNS packets are frequently filtered by the network stack. As a consequence, dynamic mDNS discovery (`_espet._tcp`) may not resolve the companion backend server running on the user's laptop.

Providing an optional "Backend Server URL" field during BLE provisioning allows users to directly configure the backend server endpoint (e.g., `http://172.20.10.2:8000`) and persist it into ESP32 NVS flash storage.

---

## 2. Architecture & Changes

### 2.1 Web Frontend: `docs/ble_prov.html`
1. **Input Field**:
   - Add `<input type="text" id="backend_url" placeholder="Backend Server URL (e.g. http://172.20.10.2:8000)" autocomplete="off">` in Step 2.
2. **Payload Delivery**:
   - When the user clicks "Connect Hotspot":
     - Read and trim `backend_url`.
     - If non-empty, strip any trailing slash. Ensure it begins with `http://` or `https://` (prepend `http://` if missing).
     - Send JSON payload to custom BLE endpoint `custom-config` using `provDevice.sendData('custom-config', new TextEncoder().encode(JSON.stringify({ backend_url })))`.
     - Log status in the live activity box.
   - Proceed with standard Wi-Fi provisioning `provDevice.provision(ssid, password)`.

### 2.2 ESP-IDF Firmware: `main/mimi_config.h`
- Define NVS namespace and key for backend configuration:
  ```c
  #define MIMI_NVS_BACKEND          "backend_cfg"
  #define MIMI_NVS_KEY_BACKEND_URL  "url"
  ```

### 2.3 ESP-IDF Firmware: `main/onboard/wifi_onboard_ble.c`
1. Before provisioning begins (`wifi_prov_mgr_start_provisioning`):
   - Call `wifi_prov_mgr_endpoint_create("custom-config");`
2. After provisioning starts:
   - Call `wifi_prov_mgr_endpoint_register("custom-config", custom_config_prov_handler, NULL);`
3. Implement `custom_config_prov_handler`:
   - Parse incoming buffer with `cJSON`.
   - If `"backend_url"` field exists:
     - Check length (< 96 bytes) and valid format.
     - Store into NVS: namespace `MIMI_NVS_BACKEND`, key `MIMI_NVS_KEY_BACKEND_URL`.
     - Respond with `{"status":"ok"}`.

### 2.4 ESP-IDF Firmware: `main/sync/backend_discovery.c`
1. Update `backend_discovery_base_url()`:
   - If runtime mDNS discovery succeeded (`s_base_url[0] != '\0'`), return `s_base_url`.
   - Else, check NVS (`MIMI_NVS_BACKEND`, `MIMI_NVS_KEY_BACKEND_URL`). If non-empty string is found, return it.
   - Else, fall back to compiled-in `MIMI_BACKEND_URL`.
2. Update `discovery_task()`:
   - If NVS has a configured `backend_url`, test or prioritize it so that mobile hotspots don't delay network tasks waiting for mDNS retries.

---

## 3. Verification Plan

1. **Firmware Compilation**:
   - Run compilation via ESP-IDF v5.5.4 environment (`python $IDF_PATH/tools/idf.py build`).
   - Verify zero compile errors or warnings regarding `wifi_prov_mgr_endpoint_*`.
2. **Web Bluetooth Interface**:
   - Open `docs/ble_prov.html` in Chrome/Edge.
   - Verify UI rendering, input behavior, and logging.
3. **Session Memory & System Overview**:
   - Update `.agent/SYSTEM_OVERVIEW.md` with the new BLE custom configuration capability according to repository guidelines.
