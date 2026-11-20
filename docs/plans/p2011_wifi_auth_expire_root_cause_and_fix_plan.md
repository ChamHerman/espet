# Wi-Fi Reboot Auth Expire / Failure Root Cause Analysis & Fix Plan

## 1. Problem Overview & Log Analysis

From the terminal screenshot provided:
```text
I (6745) wifi:new:<9,0>, old:<9,0>, ap:<255,255>, sta:<9,0>, prof:1, snd_ch_cfg:0x0
W (6745) wifi: Disconnected (reason=202:AUTH_FAIL)
W (6745) wifi: Disconnected, retry 1/5 in 1000ms
I (7755) wifi: Reconnecting to Wi-Fi...
W (10165) wifi: Disconnected (reason=205:CONNECTION_FAIL)
W (10165) wifi: Disconnected, retry 2/5 in 2000ms
I (12175) wifi: Reconnecting to Wi-Fi...
I (12185) wifi:state: init -> auth (0xb0)
I (14795) wifi:state: auth -> init (0x200)
W (14815) wifi: Disconnected (reason=2:AUTH_EXPIRE)
W (14815) wifi: Disconnected, retry 3/5 in 4000ms
I (18825) wifi: Reconnecting to Wi-Fi...
W (21235) wifi: Disconnected (reason=205:CONNECTION_FAIL)
W (21235) wifi: Disconnected, retry 4/5 in 8000ms
I (29235) wifi: Reconnecting to Wi-Fi...
I (29315) wifi:state: init -> auth (0xb0)
I (31945) wifi:state: auth -> init (0x200)
W (31955) wifi: Disconnected (reason=2:AUTH_EXPIRE)
W (31955) wifi: Disconnected, retry 5/5 in 15000ms
W (35345) mimi: WiFi connection timeout
W (35345) mimi: Entering WiFi BLE onboarding mode...
```

The device previously completed BLE Wi-Fi provisioning. Stored credentials in NVS are valid (`esmondcham`), but upon rebooting, the device cycles through `AUTH_FAIL (202)`, `CONNECTION_FAIL (205)`, and `AUTH_EXPIRE (2)`, until it abruptly drops back into BLE provisioning mode at ~35 seconds.

---

## 2. Root Cause Breakdown

### Root Cause 1: Missing Wi-Fi Deauthentication Before Reboot in BLE Provisioning
- **Mechanism**: In `main/onboard/wifi_onboard_ble.c`, when BLE provisioning succeeds, `wifi_prov_mgr` connects to the router to test the link. Once confirmed, `wifi_prov_mgr_wait()` completes, and after `vTaskDelay(2000)`, `esp_restart()` is called immediately **without calling `esp_wifi_disconnect()` or `esp_wifi_stop()`**.
- **Impact on the Router**: The home router still holds the ESP32's MAC address in its active association table (AID = 20). When the ESP32 restarts 5 seconds later and sends a fresh 802.11 Authentication frame (`auth 0xb0`), the router's state machine sees a client that it believes is already associated. The router rejects it with IEEE 802.11 Reason 2 (`WLAN_REASON_PREV_AUTH_NOT_VALID`, reported by ESP-IDF as `AUTH_EXPIRE`) or Reason 202 (`AUTH_FAIL`).
- **Duration**: The router retains this stale session until its internal inactivity timer expires (typically 30–60 seconds).

### Root Cause 2: PMF (`pmf_cfg.capable = true`) Negotiation Incompatibility
- **Mechanism**: In `main/wifi/wifi_manager.c`, `wifi_cfg` explicitly enables 802.11w Protected Management Frames:
  ```c
  .pmf_cfg = {
      .capable = true,
      .required = false,
  }
  ```
- **Impact**: Setting `capable = true` advertises PMF capability in RSN IE frames. Many residential routers and ISP routers (especially TM Unifi, Huawei, TP-Link, D-Link) fail or mismanage PMF Security Association (SA) query handshakes during client re-associations, consistently triggering Reason 202 (`AUTH_FAIL`) and Reason 2 (`AUTH_EXPIRE`). When PMF is turned off, standard WPA2 connects seamlessly.

### Root Cause 3: Premature 30s Boot Timeout vs 45s Backoff Sequence
- **Mechanism**: In `main/mimi.c`, `wifi_manager_wait_connected(30000)` enforces a hard 30-second timeout.
- **The Timing Conflict**:
  - Boot starts connecting at ~5.3s.
  - Retries 1–4 take: 1s + 2s + 4s + 8s + handshake times = ~31.9s.
  - At 31.9s, Retry 5 is queued with a 15-second backoff (`retry 5/5 in 15000ms`, scheduled for ~46.9s).
  - At 35.3s (5.3s + 30.0s), `wifi_manager_wait_connected(30000)` times out!
  - `mimi.c` aborts Wi-Fi, kills the pending 5th retry, and starts BLE onboarding.
  - The 5th retry never had the opportunity to run, right when the router's stale session table would have cleared.

### Root Cause 4: Rigid Authentication Mode Threshold
- **Mechanism**: `wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK` rejects any AP beacon advertising mixed WPA/WPA2 or transition modes. Setting threshold to `WIFI_AUTH_WPA_PSK` allows flexible negotiation.

---

## 3. Comprehensive Fix Plan

### Step 1: Clean Wi-Fi Teardown & Deauth in BLE Provisioning
**File**: `main/onboard/wifi_onboard_ble.c`
- Before calling `esp_restart()`:
  1. Call `esp_wifi_disconnect()` and delay 300ms to allow the 802.11 Deauth packet to transmit over the air.
  2. Call `esp_wifi_stop()` and delay 200ms.
  3. Ensure the router wipes the station from its active AID association table prior to reboot.

### Step 2: Configure Wi-Fi Parameters for Universal Compatibility
**File**: `main/wifi/wifi_manager.c`
- Update `wifi_cfg` initialization in `wifi_manager_start()`:
  ```c
  wifi_config_t wifi_cfg = {
      .sta = {
          .scan_method = WIFI_ALL_CHANNEL_SCAN,
          .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
          .threshold.authmode = WIFI_AUTH_WPA_PSK,
          .pmf_cfg = {
              .capable = false,
              .required = false,
          },
      },
  };
  ```
- Add masked credential diagnostic logging:
  ```c
  ESP_LOGI(TAG, "Connecting to SSID: '%s' (pass_len=%d)",
           wifi_cfg.sta.ssid, (int)strlen((char *)wifi_cfg.sta.password));
  ```
- In `on_reconnect_timer()`:
  - If reconnecting after `AUTH_EXPIRE` or `AUTH_FAIL`, ensure station state is cleanly disconnected before invoking `esp_wifi_connect()`.

### Step 3: Optimize Backoff & Align Boot Wait Timeout
**File**: `main/mimi_config.h` & `main/mimi.c`
- In `main/mimi_config.h`:
  - Set `MIMI_WIFI_RETRY_MAX_MS` to `8000` (delays: 1s, 2s, 4s, 8s, 8s = 23s total backoff).
- In `main/mimi.c`:
  - Extend `wifi_manager_wait_connected(60000)` (60 seconds).
  - Note: `wifi_manager_wait_connected()` unblocks **immediately** upon receiving `WIFI_CONNECTED_BIT` (typically in <2s) or `WIFI_FAIL_BIT` (when all retries exhaust). The 60s timeout is strictly an upper-bound safety net to prevent premature aborts.

### Step 4: Update Architecture & System Documentation
**File**: `.agent/SYSTEM_OVERVIEW.md`
- Document the Wi-Fi reconnection lifecycle, deauth-on-reboot guarantee, PMF compatibility policy, and log the change in Recent Changes.

---

## 4. Verification Plan

1. **Compilation**:
   - Run `idf.py build` via ESP-IDF v5.5.4 environment. Confirm zero errors and binary generation.
2. **Reboot Test Scenario**:
   - Perform BLE provisioning or boot with saved NVS credentials.
   - Restart the device (`restart` via Serial CLI or hardware reset button).
   - Verify terminal log:
     - `Connecting to SSID: 'esmondcham' (pass_len=X)`
     - Connects cleanly on attempt 1 or within backoff window without getting stuck in `AUTH_EXPIRE (2)` / `AUTH_FAIL (202)`.
     - Device never prematurely drops into BLE onboarding mode.
