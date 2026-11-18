# Implementation Plan: Robust Wi-Fi Boot Startup & Reset Trigger

Improve the ESPET Wi-Fi initialization procedure to eliminate startup connection collisions and retries, make reconnection non-blocking, and provide a 5-second hardware button trigger to enter BLE Provisioning mode.

## Goal Description
Analysis of the boot logs revealed three interrelated issues:
1. **Boot AP Scan Collision**: During boot, `wifi_manager_start()` initiates Wi-Fi station mode (`esp_wifi_connect()`), but immediately afterwards `app_main` in `main/mimi.c` calls `wifi_manager_scan_and_print()`. This helper explicitly invokes `esp_wifi_disconnect()` and starts a full-channel scan 10ms after connection start, causing disconnect reasons 36 (`UNKNOWN`), 202 (`AUTH_FAIL`), and 205 (`CONNECTION_FAIL`), consuming 3 of 5 allowed retries before connecting.
2. **Blocking System Event Loop**: `event_handler` in `main/wifi/wifi_manager.c` called `vTaskDelay()` directly within the ESP-IDF default event handler task (`sys_evt`), freezing all system event processing for 1000–15000ms during backoff delays.
3. **Missing Hardware Provisioning Trigger**: If the network environment changes or Wi-Fi needs reconfiguration, the user currently has to wait for retries to exhaust or use serial CLI `wifi_prov`.

This plan removes the boot scan conflict, adds WPA2/WPA3 PMF capability, implements non-blocking reconnect timers, and wires a 5-second long press on the green button (GPIO 41) to cleanly abort pending audio/camera tasks and reboot into BLE Provisioning mode while preserving stored NVS credentials.

---

## User Review Required

> [!NOTE]
> The decisions aligned upon during the interactive interview:
> 1. **AP Scan at Boot**: Removed completely from `app_main`. The `wifi_scan` serial CLI command remains available on demand.
> 2. **Retry Limit & Fallback**: Default behavior is preserved—if Wi-Fi fails after 5 retries, ESPET falls back to BLE Provisioning mode.
> 3. **Hardware Trigger (Green Button GPIO 41)**: Holding the button for 5 seconds will cancel the pending camera snapshot / audio recording, update the OLED status to `BLE Setup`, and reboot into BLE Provisioning mode without wiping stored NVS credentials.

---

## Open Questions
None. All design branches have been resolved during the `/grill-me` alignment.

---

## Proposed Changes

### Core Firmware & Wi-Fi Management

#### [MODIFY] [wifi_manager.c](file:///c:/repository/espet/main/wifi/wifi_manager.c)
- **WPA2/WPA3 PMF & Authmode**: In `wifi_manager_start()`, configure `.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK` and `.sta.pmf_cfg = { .capable = true, .required = false }` to prevent auth/association failures on modern routers.
- **Non-blocking Reconnection**: Replace `vTaskDelay()` inside `event_handler` (`WIFI_EVENT_STA_DISCONNECTED`) with a dedicated one-shot `esp_timer` (`s_reconnect_timer`). When the timer fires, it calls `esp_wifi_connect()`.
- **Timer Management**: Create `s_reconnect_timer` in `wifi_manager_init()`. Stop the timer on `IP_EVENT_STA_GOT_IP` and in `wifi_manager_stop()`.

```c
// Example timer implementation in wifi_manager.c:
static esp_timer_handle_t s_reconnect_timer = NULL;

static void on_reconnect_timer(void *arg)
{
    if (s_reconnect_enabled) {
        ESP_LOGI(TAG, "Reconnecting to AP...");
        esp_wifi_connect();
    }
}
```

#### [MODIFY] [mimi.c](file:///c:/repository/espet/main/mimi.c)
- **Eliminate Boot Scan**: Remove `wifi_manager_scan_and_print()` from lines 448–449 in `app_main`. STA connects cleanly and immediately to the stored SSID.
- **Camera/Audio Cancel Hook**: Expose a helper function (e.g., `button_cam_abort()`) to discard any in-flight camera frame buffer and disarm manual audio recording if a 5-second long press is triggered.

---

### Hardware Button & Provisioning Trigger

#### [MODIFY] [button_handler.h](file:///c:/repository/espet/main/tools/button_handler.h)
- Declare `typedef void (*button_long_press_cb_t)(void);` and register a 5-second hold callback or expose `button_handler_set_long_press_cb(button_long_press_cb_t cb)`.

#### [MODIFY] [button_handler.c](file:///c:/repository/espet/main/tools/button_handler.c)
- In `button_task`:
  - When `is_pressed` is true and `now - press_time >= 5000`:
    - Mark a flag `long_press_handled = true` so the subsequent release event does not trigger normal audio send / snapshot processing.
    - Call the registered long-press callback.
    - The callback cancels audio/camera buffers, updates the OLED screen (`display_status_set("WiFi Setup (BLE)", "Starting BLE...");`), and calls `wifi_manager_trigger_ble_prov()`.

---

### Documentation & Memory Rule

#### [MODIFY] [.agent/SYSTEM_OVERVIEW.md](file:///c:/repository/espet/.agent/SYSTEM_OVERVIEW.md)
- Update the boot sequence and button behavior documentation to document the 5s long-press BLE provisioning trigger and non-blocking Wi-Fi startup.
- Add an entry to "Recent Changes".

---

## Verification Plan

### Automated / Compilation Verification
1. Clean and build the ESP32-S3 firmware using the locally installed ESP-IDF v5.5:
   ```bash
   C:\Espressif\frameworks\esp-idf-v5.5.4\export.bat
   idf.py build
   ```
2. Verify zero linker or compilation errors.

### Manual Verification
1. **Boot Connection Test**:
   - Flash firmware to ESP32-S3: `idf.py -p COMx flash monitor`.
   - Observe the serial log on boot:
     - Verify Wi-Fi connects on the very first attempt (0 retries, reason 36/202/205 gone).
     - Verify elapsed time to connection drops from ~16s down to ~1–3s.
2. **5-Second Green Button Hold Test**:
   - Press and hold the green button (GPIO 41) for 5 seconds.
   - Verify OLED immediately displays `WiFi Setup (BLE) / Starting BLE...`.
   - Verify ESP32 reboots into BLE Provisioning mode.
   - Verify stored NVS credentials were not erased.
3. **Short Button Press Test**:
   - Verify normal short press / multimodal press-and-hold (< 5s) still functions as camera snapshot / voice PTT.
