#pragma once

/* JK BMS (JiKong) BLE driver — frame reassembly and decode.
 *
 * Deliberately free of NimBLE and ESP-IDF so it can be compiled and fuzzed on
 * the host against captured frames; the transport hands it bytes and nothing
 * else. See tools/test/test_bms_jk.c.
 *
 * Wire format (BLE only — the UART/RS485 side of JK uses a different framing
 * that starts with 0x4E 0x57, do NOT reuse this parser for it):
 *
 *   Responses (BMS -> us), fixed 300 bytes:
 *     [0..3]   0x55 0xAA 0xEB 0x90
 *     [4]      frame type: 0x01 settings, 0x02 cell info, 0x03 device info
 *     ...      payload
 *     [299]    checksum = 8-bit sum of bytes 0..298
 *
 *   Commands (us -> BMS), fixed 20 bytes:
 *     [0..3]   0xAA 0x55 0x90 0xEB   <-- the response header, byte-swapped
 *     [4]      register / command
 *     [5]      data length (0x00 or 0x04)
 *     [6..9]   value, little-endian u32
 *     [10..18] zero padding
 *     [19]     checksum = 8-bit sum of bytes 0..18
 *
 * The two headers being mirror images of each other is a real trap: sending a
 * command with the response header produces no error, the BMS simply ignores
 * it and the link looks connected but mute.
 *
 * Response frames arrive split across several ATT notifications, so the caller
 * feeds fragments and we reassemble.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bms/bms_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JK_FRAME_LEN        300
#define JK_CMD_LEN          20

#define JK_FRAME_SETTINGS   0x01
#define JK_FRAME_CELL_INFO  0x02
#define JK_FRAME_DEVICE_INFO 0x03

/* Command registers. Sources disagree on which one yields the cell-info frame:
 * syssi/esphome-jk-bms names 0x96 COMMAND_CELL_INFO, while the taraskinua
 * write-up maps 0x95 to telemetry (response type 0x02) and 0x96 to settings
 * (type 0x01). This does not endanger decoding — the parser dispatches on the
 * response's own type byte, not on what we asked for — but if cell data never
 * arrives, swapping JK_CMD_CELL_INFO to JK_CMD_TELEMETRY is the first thing to
 * try. Verify against a real pack; nobody has confirmed it here. */
#define JK_CMD_DEVICE_INFO  0x97
#define JK_CMD_CELL_INFO    0x96
#define JK_CMD_TELEMETRY    0x95
#define JK_CMD_SETTINGS     0x96

/* Which byte layout the connected unit speaks. The two differ by a fixed 32
 * byte shift for everything past the cell array, because the 32S variant
 * carries eight more cells. There is no way to tell them apart from the cell
 * frame itself — it must come from the device-info frame, which is exactly
 * why the handshake asks for 0x97 before 0x96. */
typedef enum {
    JK_PROTO_UNKNOWN = 0,
    JK_PROTO_02_24S,
    JK_PROTO_02_32S,
} jk_proto_t;

typedef struct {
    uint8_t    buf[JK_FRAME_LEN];
    uint16_t   len;          /* bytes accumulated in buf                   */
    bool       in_frame;     /* preamble seen, collecting                  */
    jk_proto_t proto;
    char       hw_version[16];
    char       sw_version[16];
} jk_ctx_t;

void jk_init(jk_ctx_t *ctx);

/* Raw bytes of the most recently completed frame. Valid only until the next
 * one arrives; intended for bring-up logging, not for decoding. */
const uint8_t *jk_frame_buf(const jk_ctx_t *ctx);

/* Force the layout when auto-detection cannot decide (user override). */
void jk_set_proto(jk_ctx_t *ctx, jk_proto_t proto);
jk_proto_t jk_get_proto(const jk_ctx_t *ctx);

/* Build a 20-byte command frame. Returns bytes written, or 0 if cap is too
 * small. */
size_t jk_build_cmd(uint8_t *out, size_t cap, uint8_t cmd, uint32_t value);

typedef enum {
    JK_FEED_NEED_MORE = 0,  /* fragment consumed, frame incomplete          */
    JK_FEED_SNAPSHOT,       /* a cell-info frame decoded into *out          */
    JK_FEED_DEVICE_INFO,    /* device-info frame parsed into ctx versions   */
    JK_FEED_SETTINGS,       /* settings frame seen; nothing decoded from it */
    JK_FEED_IGNORED,        /* complete frame of a type we do not decode    */
    JK_FEED_CRC_ERROR,
} jk_feed_result_t;

/* Feed one notification fragment. *out is only written when the result is
 * JK_FEED_SNAPSHOT. Safe to call with len 0.
 *
 * *consumed (may be NULL) receives how many bytes of `data` were taken. On any
 * result other than JK_FEED_NEED_MORE the call stops at the end of the frame
 * it completed, so the caller MUST resume at data + *consumed rather than
 * assuming the slice is finished: the bytes after a completed frame are the
 * head of the next one, preamble included, and dropping them costs a whole
 * frame every time a transport chunk straddles a frame boundary. */
jk_feed_result_t jk_feed(jk_ctx_t *ctx, const uint8_t *data, size_t len,
                         bms_snapshot_t *out, size_t *consumed);

#ifdef __cplusplus
}
#endif
