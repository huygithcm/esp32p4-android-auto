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
    protocol's usual x1000 floats: currents are amps x 10, the reverse speed is
    km/h x 10. Both ends then agree exactly, and a number the rider typed
    cannot come back a rounding step from what they set.

    Format 2 replaced the per-mode speed and per-mille scale with one absolute
    ampere figure per mode, and added esc_current_max_dA to every reply so the
    screen can show requested against effective. A format 1 parser must not
    read a format 2 payload: the version byte sits before every field that
    moved, and both sides refuse on mismatch.
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
/* Status poll. Not in the original contract, which left the mechanism open:
 * re-SELECTing the current profile was the obvious way and is wrong, because
 * the P4's cached profile is stale the instant the rider presses the TX button
 * and re-asserting it drags the mode back. This asks and changes nothing. */
#define VRM_MSG_REQ_STATUS    0x0Au
#define VRM_MSG_REQ_SAFETY    0x0Bu
#define VRM_MSG_SET_PARK      0x0Cu
#define VRM_MSG_REQ_STATUS_SEQ 0x0Du
#define VRM_MSG_STATUS_SEQ    0x8Du
#define VRM_STATUS_SEQ_MSG_LEN 20u
/* 0x0D request: COMM,V,P,id,reply_id,seq:u16.
 * 0x8D reply: COMM,V,P,id,seq:u16, followed by the legacy status payload.
 * Legacy 0x89 remains parseable but cannot refresh the live transport. */
#define VRM_MSG_SAFETY        0x8Bu
#define VRM_MSG_PARK_ACK      0x8Cu
#define VRM_SAFETY_MSG_LEN    10u
/* Reply: COMM, V, P, id, version, seq:u16, state, profile, result.
 * SET consumes the latest acknowledged query sequence as a one-shot token.
 * Its distinct reply id prevents a duplicate query from acknowledging SET. */
bool vesc_ride_mode_parse_safety(const uint8_t *data, unsigned int len,
                               vesc_ride_safety_t *out);
bool vesc_ride_safety_reply_matches(uint16_t received, uint16_t pending,
                                  uint32_t sent_ms, uint32_t now_ms);
/* Lisp -> P4 */
#define VRM_MSG_CONFIG        0x87u
#define VRM_MSG_STATUS        0x89u

/* Exact on-wire lengths, COMM byte included. Anything shorter is refused
 * before a single field is read.
 *
 * FORMAT 2 layouts, in full:
 *
 *   0x87 RIDE_CONFIG   (24 bytes, P4 side, COMM byte at [0])
 *     [0] COMM  [1..2] magic  [3] 0x87
 *     [4..5] seq   [6] result  [7] format_version
 *     [8..9] config_revision
 *     [10..11] mode0_dA  [12..13] mode1_dA  [14..15] mode2_dA
 *     [16] reverse_enabled
 *     [17..18] reverse_speed_dkmh  [19..20] reverse_current_dA
 *     [21..22] esc_current_max_dA
 *     [23] persist_pending
 *
 *   0x89 RIDE_STATUS   (18 bytes)
 *     [0] COMM  [1..2] magic  [3] 0x89
 *     [4..5] config_revision  [6] current_profile
 *     [7..8] requested_current_dA  [9..10] effective_current_dA
 *     [11..12] esc_current_max_dA
 *     [13] direction_state (i8)  [14] reverse_button  [15] reverse_armed
 *     [16] persist_pending  [17] fault_reason
 *
 *   0x08 SET_CONFIG    (18 bytes as Lisp sees it, i.e. no COMM byte)
 *     [0..1] magic  [2] 0x08  [3] reply_id  [4..5] seq  [6] format_version
 *     [7..8] mode0_dA  [9..10] mode1_dA  [11..12] mode2_dA
 *     [13] reverse_enabled
 *     [14..15] reverse_speed_dkmh  [16..17] reverse_current_dA
 */
#define VRM_CONFIG_MSG_LEN    24u
#define VRM_STATUS_MSG_LEN    18u
/* What the Lisp side must see before it may read a SET payload. */
#define VRM_SET_MSG_LEN       18u

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
