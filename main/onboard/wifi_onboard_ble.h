#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start BLE Wi-Fi provisioning service.
 * Advertises PROV_ESPET_XXXX over BLE, handles Wi-Fi scanning and
 * credential exchange with phone app/web, tests connection,
 * saves credentials to NVS, and restarts device.
 * Blocks until provisioning finishes.
 */
esp_err_t wifi_onboard_ble_start(void);

#ifdef __cplusplus
}
#endif
