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

/* Visibility gate. The BMS tab calls this as it is shown and hidden.
 *
 * Inactive stops the cell-info poll but KEEPS the link up: the BLE radio is
 * on the C6, sharing its SDIO path with Wi-Fi, so a 1 Hz poll running behind
 * an Android Auto video stream costs bandwidth the video needs. Holding the
 * connection rather than dropping it means reopening the tab shows data in a
 * second instead of a scan-and-connect cycle, and it keeps the negotiated
 * MTU and the detected layout.
 *
 * Defaults to active so a build that never calls this behaves as before. */
void ble_bms_set_active(bool active);

#ifdef __cplusplus
}
#endif
