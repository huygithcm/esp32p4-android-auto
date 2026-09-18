/*
 * Internal interface between the runtime (table selection, values, emulator,
 * public API) and the transport (CAN request/response state machine). Not part
 * of the public include surface.
 */
#pragma once

#include "vesc_config/vesc_config.h"
#include "vesc_config/vesc_config_serdes.h"
#include "vesc_config/vesc_config_types.h"

#include <stdbool.h>
#include <stdint.h>

/* Completion callback for an async CAN op. Fires from the CAN process task on
 * success, or from the esp_timer task on timeout. */
typedef void (*vct_cb_t)(vc_result_t res, void *user);
typedef void (*vct_fw_cb_t)(uint8_t major, uint8_t minor, bool ok, void *user);

void vct_init(void);

/* Async GET of mcconf/appconf (or its firmware defaults) into vals_out.
 * Returns VC_OK if the request was sent, else VC_ERR_BUSY. */
vc_result_t vct_get(const vc_table_t *t, bool defaults, vc_value_t *vals_out,
                    vct_cb_t cb, void *user);

/* Async SET — serializes vals internally and writes it to the VESC. */
vc_result_t vct_set(const vc_table_t *t, const vc_value_t *vals,
                    vct_cb_t cb, void *user);

/* Async COMM_FW_VERSION probe of the configured target VESC. */
vc_result_t vct_probe_fw(vct_fw_cb_t cb, void *user);

/* FOC detection (COMM_DETECT_APPLY_ALL_FOC): spins the motor, measures R/L/flux
 * + hall/encoder, applies to the running config. Long timeout (motor spins for
 * seconds). cb result: >=0 success (motor count), <0 firmware error code,
 * VCT_DETECT_TIMEOUT on no reply. */
#define VCT_DETECT_TIMEOUT (-1000)
typedef void (*vct_detect_cb_t)(int result, void *user);
vc_result_t vct_detect_foc(bool detect_can, double max_power_loss, double min_current_in,
                           double max_current_in, double openloop_rpm, double sl_erpm,
                           vct_detect_cb_t cb, void *user);

/* Generic request used for the manual detection commands (R/L, flux linkage,
 * hall, encoder): sends [cmd][body], waits (long timeout — these spin the motor)
 * for a reply whose COMM id == cmd, and hands the raw payload (after the id byte)
 * to cb. ok=0 on timeout. The runtime does the per-command parsing. */
typedef void (*vct_raw_cb_t)(int ok, const uint8_t *payload, unsigned int len, void *user);
vc_result_t vct_request_raw(uint8_t cmd, const uint8_t *body, unsigned int body_len,
                            unsigned int timeout_ms, vct_raw_cb_t cb, void *user);

/* Returns true if a request is currently in flight. */
bool vct_busy(void);
