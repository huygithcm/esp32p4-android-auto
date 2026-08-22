/*
    Copyright 2026 Adapted to ESP-IDF for ESP32-P4 (GPL-3.0).

    Wire constants and pure parsers for the ride-mode/reverse backend.

    vesc_ride_mode.h is the FE-facing contract and belongs to the screen: types
    and the non-blocking calls it uses. This header carries what only the CAN
    layer and its host tests need, so the two can be edited without landing on
    each other. Nothing here is duplicated from that file.

    Protocol, from docs/RIDE_MODE_REVERSE_BE_CONTRACT.md section 4. Big-endian,
    sharing the COMM_CUSTOM_APP_DATA / 'VP' channel with vesc_lisp_panel.h. The
    COMM byte is added and stripped by the firmware; it is present in what the
    P4 dispatcher receives and absent in what Lisp sees, which is why the
    lengths below count it and the Lisp side's do not.

        P4 -> Lisp
        0x07 REQ_RIDE_CONFIG  [u8 reply_id][u16 seq]
        0x08 SET_RIDE_CONFIG  [u8 reply_id][u16 seq][u8 format_ver]
                              [u16 speed_dkmh][u16 current_permille] x3
                              [u8 reverse_enabled]
                              [u16 reverse_speed_dkmh][u16 reverse_current_dA]
        0x09 SELECT_RIDE_MODE [u8 reply_id][u16 seq][u8 profile]

        Lisp -> P4
        0x87 RIDE_CONFIG      [u16 seq][u8 result][u8 format_ver][u16 revision]
                              [u16 speed_dkmh][u16 current_permille] x3
                              [u8 reverse_enabled]
                              [u16 reverse_speed_dkmh][u16 reverse_current_dA]
                              [u8 persist_pending]
        0x89 RIDE_STATUS      [u16 revision][u8 profile][u16 active_speed_dkmh]
                              [i8 direction][u8 button][u8 armed]
                              [u8 persist_pending][u8 fault_reason]

    Values travel as the data model's scaled integers rather than this
    protocol's usual x1000 floats: speeds are km/h x 10, current scale is
    per-mille. Both ends then agree exactly, and a number the rider typed
    cannot come back a rounding step from what they set.
*/

#pragma once

#include "vesc_can/vesc_ride_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared with vesc_lisp_panel: the two protocols ride the same channel and are
 * told apart by message id alone, so the magic must be identical. */
#define VLP_MAGIC0            0x56u   /* 'V' */
#define VLP_MAGIC1            0x50u   /* 'P' */

/* P4 -> Lisp */
#define VRM_MSG_REQ_CONFIG    0x07u
#define VRM_MSG_SET_CONFIG    0x08u
#define VRM_MSG_SELECT_MODE   0x09u
/* Lisp -> P4 */
#define VRM_MSG_CONFIG        0x87u
#define VRM_MSG_STATUS        0x89u

/* Exact on-wire lengths, COMM byte included. Anything shorter is refused
 * before a single field is read. */
#define VRM_CONFIG_MSG_LEN    28u
#define VRM_STATUS_MSG_LEN    14u

/* Pure parsers, exposed for the host tests. They write *out only when they
 * return true and never read past `len`.
 *
 * parse_config returns true for a well-formed reply even when it carries a
 * refusal: out->last_result says which, and out->valid says whether the
 * numbers alongside it are usable. A reply in a format version this build
 * does not know returns true with valid=false and BAD_VERSION, because the
 * sequence number is still trustworthy and a Save waiting on it deserves an
 * answer rather than a timeout. */
bool vesc_ride_mode_parse_config(const uint8_t *data, unsigned int len,
                                 vesc_ride_config_t *out);
bool vesc_ride_mode_parse_status(const uint8_t *data, unsigned int len,
                                 vesc_ride_status_t *out);

/* Range check from contract section 3, shared by the editor and the tests so
 * the screen can refuse Save for the same reasons Lisp would. Lisp re-validates
 * with the throttle and speed in hand, which this side cannot see: this is a
 * convenience, never the gate. */
bool vesc_ride_mode_config_in_range(const vesc_ride_config_t *cfg,
                                    vesc_ride_result_t *why_out);

#ifdef __cplusplus
}
#endif
