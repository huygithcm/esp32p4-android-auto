#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up NimBLE host on top of the ESP-Hosted VHCI controller (C6).
 *
 * Order: esp_hosted_bt_controller_init/enable → nimble_port_init →
 * register host callbacks + GATT services → start NimBLE host task.
 * On host sync, advertising starts automatically. Idempotent. */
esp_err_t ble_host_init(void);

/* True while a peer (e.g. VESC Tool / nRF Connect) is connected to the
 * NimBLE GATT server. Safe to call from any task; backed by a volatile
 * flag set from the GAP event handler. Used by the dashboard to colour
 * the BT status indicator. */
bool ble_host_is_connected(void);

#ifdef __cplusplus
}
#endif
