#pragma once

/* BLE central link to a JK BMS.
 *
 * Mirrors ble_cadence_client: its own GAP callback, its own connection, and
 * it never touches the peripheral advertising path in ble_host.c. What it
 * adds over the cadence link is a write direction — JK only speaks when
 * asked — and a worker task, because a 300-byte frame arrives split across
 * ATT notifications and must not be reassembled inside the NimBLE host task.
 *
 * Decoded data leaves through components/bms (bms_model_get); nothing here is
 * exposed to the UI directly. The BMS tab must not call into this file.
 */

#include <stdbool.h>
#include <stdint.h>

#include "host/ble_hs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reported for every JK-looking peer seen during a scan, so the settings UI
 * can offer a pick list. name may be empty when the peer put it in the scan
 * response and we have not seen that yet. */
typedef void (*ble_bms_scan_cb_t)(const ble_addr_t *addr, const char *name,
                                  int8_t rssi);

void ble_bms_client_init(void);
void ble_bms_on_ble_sync(uint8_t own_addr_type);

void ble_bms_set_scan_cb(ble_bms_scan_cb_t cb);
void ble_bms_scan_start(void);
void ble_bms_scan_stop(void);

/* Bind to a peer and start connecting; pass NULL to unbind, drop the link and
 * clear the published snapshot. The address is not persisted here — the
 * settings layer owns NVS. */
void ble_bms_bind(const ble_addr_t *addr);
bool ble_bms_get_bound(ble_addr_t *out);

bool ble_bms_is_connected(void);

#ifdef __cplusplus
}
#endif
