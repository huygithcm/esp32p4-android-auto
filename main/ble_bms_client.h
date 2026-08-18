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
void ble_bms_scan_stop(void);

/* Returns false when the scan could not be started — NimBLE allows one
 * initiator at a time, so a connect already in flight blocks the scanner.
 * The UI previously had no way to tell "nothing nearby" from "we never
 * actually looked", and showed an empty list either way. */
bool ble_bms_scan_start(void);
bool ble_bms_scan_is_active(void);

/* Bind to a peer and start connecting; pass NULL to unbind, drop the link and
 * clear the published snapshot. Rebinding over a live link tears the old one
 * down first.
 *
 * The address is NOT written to NVS here. It is persisted only once the peer
 * has answered device-info with a layout we recognise — otherwise a mistaken
 * tap on a neighbour's pack would come back on every boot. Storage lives in
 * this module (namespace "bms_ble"), not in dev_settings: it is backend state
 * and dev_settings is an upstream file the UI half also edits. */
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
 * Starts INACTIVE, matching the tab's own initial state. The two must agree:
 * bms_view zeroes its struct on create, and its setter early-returns when the
 * requested state already matches what it thinks is current. If the backend
 * started active, that first set_active(false) would be swallowed by the
 * guard and the poll would run forever with nobody watching — which is the
 * failure this gate exists to prevent. Connecting and the device-info probe
 * are NOT gated, so the layout is detected either way and opening the tab
 * shows data immediately. */
void ble_bms_set_active(bool active);

#ifdef __cplusplus
}
#endif
