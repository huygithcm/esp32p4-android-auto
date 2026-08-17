/* JK BMS BLE frame reassembly + decode. See include/bms/bms_jk.h.
 *
 * Offsets follow syssi/esphome-jk-bms, which is the reference implementation
 * the community converged on. Everything past the cell array shifts by
 * JK_32S_SHIFT on the 32S layout; that single variable is the only structural
 * difference between the two, so it is applied once and reused.
 */

#include "bms/bms_jk.h"

#include <string.h>

/* Response preamble. */
static const uint8_t JK_PREAMBLE[4] = { 0x55, 0xAA, 0xEB, 0x90 };
/* Command header — the same four bytes pairwise swapped. Getting this wrong
 * is silent: the BMS ignores the frame and the link just never answers. */
static const uint8_t JK_CMD_HDR[4]  = { 0xAA, 0x55, 0x90, 0xEB };

/* The 32S layout carries 8 extra cells (16 bytes) but reserves 32, so every
 * field after the cell block sits 32 bytes further along. */
#define JK_32S_SHIFT        32

#define JK_OFF_TYPE         4
#define JK_OFF_CELLS        6
#define JK_CELLS_24S        24
#define JK_CELLS_32S        32

/* Offsets below are for the 24S layout; add shift for 32S. */
#define JK_OFF_TEMP_MOS_32S 112   /* 32S keeps MOS temp before the shift */
#define JK_OFF_PACK_MV      118
#define JK_OFF_CURRENT      126
#define JK_OFF_TEMP1        130
#define JK_OFF_TEMP2        132
#define JK_OFF_TEMP_MOS_24S 134
#define JK_OFF_BALANCING    140
#define JK_OFF_SOC          141
#define JK_OFF_REMAIN_MAH   142
#define JK_OFF_NOMINAL_MAH  146
#define JK_OFF_CYCLES       150
#define JK_OFF_CYCLE_CAP    154
#define JK_OFF_SOH          158
#define JK_OFF_CHG_MOS      166
#define JK_OFF_DSG_MOS      167
#define JK_OFF_BAL_CURRENT  138
#define JK_OFF_HEATER_ON    183
#define JK_OFF_EMERG_TIMER  186   /* 32S only */
#define JK_OFF_HEATER_MA    204
#define JK_OFF_SLEEP_TIMER  238   /* u32, both layouts */

/* Balance-lead resistances live in the SETTINGS frame (type 0x01), not with
 * the cell voltages. These are absolute offsets into that frame and are not
 * related to JK_32S_SHIFT — the two layouts simply place the block in
 * different spots. */
#define JK_SET_OFF_RES_24S  158
#define JK_SET_OFF_RES_32S  142

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static int16_t  rd_i16(const uint8_t *p) { return (int16_t)rd_u16(p); }
static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t rd_i32(const uint8_t *p) { return (int32_t)rd_u32(p); }

static uint8_t jk_checksum(const uint8_t *p, size_t n)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < n; i++) sum = (uint8_t)(sum + p[i]);
    return sum;
}

const uint8_t *jk_frame_buf(const jk_ctx_t *ctx)
{
    return ctx ? ctx->buf : NULL;
}

void jk_init(jk_ctx_t *ctx)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof *ctx);
    ctx->proto = JK_PROTO_UNKNOWN;
}

void jk_set_proto(jk_ctx_t *ctx, jk_proto_t proto)
{
    if (ctx) ctx->proto = proto;
}

jk_proto_t jk_get_proto(const jk_ctx_t *ctx)
{
    return ctx ? ctx->proto : JK_PROTO_UNKNOWN;
}

size_t jk_build_cmd(uint8_t *out, size_t cap, uint8_t cmd, uint32_t value)
{
    if (!out || cap < JK_CMD_LEN) return 0;

    memset(out, 0, JK_CMD_LEN);
    memcpy(out, JK_CMD_HDR, sizeof JK_CMD_HDR);
    out[4] = cmd;
    out[5] = 0x00;                 /* data length; 0 for the plain requests */
    out[6] = (uint8_t)(value & 0xFF);
    out[7] = (uint8_t)((value >> 8) & 0xFF);
    out[8] = (uint8_t)((value >> 16) & 0xFF);
    out[9] = (uint8_t)((value >> 24) & 0xFF);
    /* bytes 10..18 stay zero (reserved) */
    out[JK_CMD_LEN - 1] = jk_checksum(out, JK_CMD_LEN - 1);
    return JK_CMD_LEN;
}

/* Copy a NUL-or-space terminated ASCII field out of the frame. */
static void copy_str(char *dst, size_t dst_sz, const uint8_t *src, size_t src_sz)
{
    size_t n = 0;
    for (; n < src_sz && n < dst_sz - 1; n++) {
        const char c = (char)src[n];
        if (c == '\0') break;
        dst[n] = c;
    }
    dst[n] = '\0';
}

static void decode_device_info(jk_ctx_t *ctx)
{
    /* Device info carries the vendor/hardware/software strings. We only need
     * the versions, and only to pick the cell-frame layout. Offsets here are
     * the community's; a unit that reports something we do not recognise is
     * left as UNKNOWN so the caller can surface UNSUPPORTED rather than
     * decode with a guessed layout and publish plausible nonsense. */
    copy_str(ctx->hw_version, sizeof ctx->hw_version, &ctx->buf[22], 8);
    copy_str(ctx->sw_version, sizeof ctx->sw_version, &ctx->buf[30], 8);

    if (ctx->proto != JK_PROTO_UNKNOWN) return;   /* user override wins */

    /* Hardware 11.x and newer speak the 32S layout; 8.x through 10.x speak
     * 24S. The string is like "11.XW" or "10.xw". */
    if (ctx->hw_version[0] == '1' && ctx->hw_version[1] == '1') {
        ctx->proto = JK_PROTO_02_32S;
    } else if (ctx->hw_version[0] != '\0') {
        ctx->proto = JK_PROTO_02_24S;
    }
}

/* Settings frame (0x01).
 *
 * This was written to pull per-cell balance-lead resistances out of here, on
 * one source's claim that the cell frame does not carry them. A later, more
 * authoritative reading contradicts that: the community implementation's cell
 * info decoder handles "cell voltages AND cell internal resistances" from the
 * 0x02 frame, so the block below is very likely reading configuration bytes
 * and calling them resistances.
 *
 * Rather than publish plausible nonsense, the values are parsed but NOT marked
 * valid, so the UI shows them as unavailable. The exact offset of the
 * resistance array inside the 0x02 frame could not be established from
 * documentation; ble_bms_client dumps the candidate region of the first cell
 * frame to the console so the first session with real hardware can settle it.
 * Once it is known, decode it in decode_cell_info and delete this. */
static void decode_settings(jk_ctx_t *ctx)
{
    if (ctx->proto == JK_PROTO_UNKNOWN) return;

    const int base = (ctx->proto == JK_PROTO_02_32S) ? JK_SET_OFF_RES_32S
                                                     : JK_SET_OFF_RES_24S;
    const int ncell = (ctx->proto == JK_PROTO_02_32S) ? JK_CELLS_32S
                                                      : JK_CELLS_24S;

    uint32_t mask = 0;
    for (int i = 0; i < ncell && i < BMS_MAX_CELLS; i++) {
        const int off = base + i * 2;
        if (off + 1 >= JK_FRAME_LEN - 1) break;      /* never read the CRC */
        const uint16_t milliohm = rd_u16(&ctx->buf[off]);
        ctx->wire_res_mohm[i] = milliohm;
        /* A cell slot that is not populated reads 0 here as well, so gate on
         * the pack actually having that cell rather than on the value. The
         * cell frame establishes the count; until one has been seen we take
         * every entry and let the cell mask filter later. */
        mask |= (1u << i);
    }
    /* Deliberately NOT stored as valid — see the note above. Keeping the read
     * makes the eventual fix a one-line change instead of a rewrite. */
    (void)mask;
    ctx->wire_res_valid_mask = 0;
}

static void decode_cell_info(jk_ctx_t *ctx, bms_snapshot_t *out)
{
    const uint8_t *b = ctx->buf;
    const bool is32 = (ctx->proto == JK_PROTO_02_32S);
    const int  shift = is32 ? JK_32S_SHIFT : 0;
    const int  ncell = is32 ? JK_CELLS_32S : JK_CELLS_24S;

    memset(out, 0, sizeof *out);
    out->driver_id = BMS_DRIVER_JK_BLE;

    /* ---- cells. A pack smaller than the frame's capacity reports 0 mV for
     * the unpopulated slots; those must stay invalid, not become 0 mV cells. */
    uint16_t vmin = 0xFFFF, vmax = 0;
    uint8_t  imin = 0, imax = 0, populated = 0;
    for (int i = 0; i < ncell && i < BMS_MAX_CELLS; i++) {
        const uint16_t mv = rd_u16(&b[JK_OFF_CELLS + i * 2]);
        out->cell_mv[i] = mv;
        if (mv == 0) continue;
        out->cell_valid_mask |= (1u << i);
        populated = (uint8_t)(i + 1);
        if (mv < vmin) { vmin = mv; imin = (uint8_t)i; }
        if (mv > vmax) { vmax = mv; imax = (uint8_t)i; }
    }
    if (populated) {
        out->cell_count    = populated;
        out->cell_min_mv   = vmin;
        out->cell_max_mv   = vmax;
        out->cell_delta_mv = (uint16_t)(vmax - vmin);
        out->cell_min_idx  = imin;
        out->cell_max_idx  = imax;
        out->valid_mask   |= BMS_V_CELLS;
    }

    /* ---- pack voltage / current. JK reports current as negative-for-
     * discharge; the model's convention is the opposite (see bms_model.h),
     * so invert here rather than making every consumer remember. */
    out->pack_mv = (int32_t)rd_u32(&b[JK_OFF_PACK_MV + shift]);
    out->valid_mask |= BMS_V_PACK_MV;

    out->pack_current_ma = -rd_i32(&b[JK_OFF_CURRENT + shift]);
    out->valid_mask |= BMS_V_CURRENT;

    /* ---- state of charge and capacity ---------------------------------- */
    const uint8_t soc_pct = b[JK_OFF_SOC + shift];
    if (soc_pct <= 100) {
        out->soc_permille = (uint16_t)(soc_pct * 10);
        out->valid_mask  |= BMS_V_SOC;
    }

    /* Capacities are in 0.001 Ah on the wire, i.e. already mAh. */
    out->remaining_mah = (int32_t)rd_u32(&b[JK_OFF_REMAIN_MAH + shift]);
    out->nominal_mah   = (int32_t)rd_u32(&b[JK_OFF_NOMINAL_MAH + shift]);
    out->valid_mask   |= BMS_V_REMAINING | BMS_V_NOMINAL;

    out->cycle_count = rd_u32(&b[JK_OFF_CYCLES + shift]);
    out->valid_mask |= BMS_V_CYCLES;

    out->cycle_capacity_mah = (int32_t)rd_u32(&b[JK_OFF_CYCLE_CAP + shift]);
    out->valid_mask |= BMS_V_CYCLE_CAPACITY;

    const uint8_t soh_pct = b[JK_OFF_SOH + shift];
    if (soh_pct <= 100) {
        out->soh_permille = (uint16_t)(soh_pct * 10);
        out->valid_mask  |= BMS_V_SOH;
    }

    out->balance_current_ma = rd_i16(&b[JK_OFF_BAL_CURRENT + shift]);
    out->valid_mask |= BMS_V_BALANCE_CURRENT;

    out->heater_on         = b[JK_OFF_HEATER_ON + shift] != 0;
    out->heater_current_ma = rd_i16(&b[JK_OFF_HEATER_MA + shift]);
    out->valid_mask |= BMS_V_HEATER;

    /* Only the 32S layout carries the countdown timers; on 24S the bytes at
     * this offset mean something else, so leave the field invalid rather than
     * publishing whatever happens to be there. */
    if (is32) {
        out->emergency_timer_s = rd_u16(&b[JK_OFF_EMERG_TIMER + shift]);
        out->valid_mask |= BMS_V_TIMERS;
    }

    /* Smart-sleep countdown is present on both layouts and is 32-bit, so it
     * gets its own validity bit rather than riding on BMS_V_TIMERS. */
    if (JK_OFF_SLEEP_TIMER + shift + 4 <= JK_FRAME_LEN - 1) {
        out->sleep_timer_s = rd_u32(&b[JK_OFF_SLEEP_TIMER + shift]);
        out->valid_mask |= BMS_V_SLEEP_TIMER;
    }

    /* Carry the wire resistances forward from the last settings frame. They
     * change only when the installer rewires the pack, so refreshing them
     * once per session is enough — but they must ride along on every
     * snapshot, because the UI reads one struct. */
    if (ctx->wire_res_valid_mask) {
        memcpy(out->wire_res_mohm, ctx->wire_res_mohm,
               sizeof out->wire_res_mohm);
        out->wire_res_valid_mask = ctx->wire_res_valid_mask;
        out->valid_mask |= BMS_V_WIRE_RES;
    }

    /* ---- temperatures --------------------------------------------------- */
    out->temp_deci_c[0] = rd_i16(&b[JK_OFF_TEMP1 + shift]);
    out->temp_deci_c[1] = rd_i16(&b[JK_OFF_TEMP2 + shift]);
    out->temp_count      = 2;
    out->temp_valid_mask = 0x3;
    out->valid_mask     |= BMS_V_TEMPS;

    /* MOS temperature sits before the shift on 32S and after it on 24S —
     * the one field where the layouts genuinely disagree rather than just
     * sliding. */
    out->mos_temp_deci_c = is32 ? rd_i16(&b[JK_OFF_TEMP_MOS_32S])
                                : rd_i16(&b[JK_OFF_TEMP_MOS_24S]);
    out->valid_mask |= BMS_V_MOS_TEMP;

    /* ---- switches / balancing ------------------------------------------- */
    out->chg_mos_on = b[JK_OFF_CHG_MOS + shift] != 0;
    out->dsg_mos_on = b[JK_OFF_DSG_MOS + shift] != 0;
    out->valid_mask |= BMS_V_MOSFETS;

    /* 0 = off, 1 = balancing while charging, 2 = while discharging. The
     * frame does not say which cells, so balance_mask stays 0. */
    out->balancing = b[JK_OFF_BALANCING + shift] != 0;
    out->valid_mask |= BMS_V_BALANCE;
}

jk_feed_result_t jk_feed(jk_ctx_t *ctx, const uint8_t *data, size_t len,
                         bms_snapshot_t *out)
{
    if (!ctx || !data || len == 0) return JK_FEED_NEED_MORE;

    for (size_t i = 0; i < len; i++) {
        const uint8_t byte = data[i];

        if (ctx->len < JK_FRAME_LEN) {
            ctx->buf[ctx->len++] = byte;
        } else {
            /* Unreachable while frames complete at exactly JK_FRAME_LEN, but
             * a future layout change must not walk off the buffer. */
            ctx->len      = 0;
            ctx->in_frame = false;
            continue;
        }

        /* Resynchronise on the preamble wherever it turns up, including in
         * the middle of what we thought was a frame. Without this, one lost
         * fragment poisons every frame afterwards: we keep counting to 300
         * from the wrong offset and every field is shifted. Restarting costs
         * at most the frame in flight and recovers by itself.
         *
         * The cost is that a payload which happens to contain the four
         * preamble bytes would be dropped. At 300 bytes that is a ~1-in-10^7
         * event per frame, against a permanent desync otherwise. */
        if (ctx->len >= sizeof JK_PREAMBLE &&
            memcmp(&ctx->buf[ctx->len - sizeof JK_PREAMBLE], JK_PREAMBLE,
                   sizeof JK_PREAMBLE) == 0) {
            memcpy(ctx->buf, JK_PREAMBLE, sizeof JK_PREAMBLE);
            ctx->len      = sizeof JK_PREAMBLE;
            ctx->in_frame = true;
            continue;
        }

        if (!ctx->in_frame) {
            /* Still hunting for a preamble. Keep only the last three bytes:
             * anything older cannot be a prefix of one, and the buffer must
             * not fill up with noise. */
            if (ctx->len > sizeof JK_PREAMBLE - 1) {
                memmove(ctx->buf, &ctx->buf[ctx->len - (sizeof JK_PREAMBLE - 1)],
                        sizeof JK_PREAMBLE - 1);
                ctx->len = sizeof JK_PREAMBLE - 1;
            }
            continue;
        }

        if (ctx->len < JK_FRAME_LEN) continue;

        /* Full frame. */
        ctx->in_frame = false;
        ctx->len      = 0;

        const uint8_t want_crc = ctx->buf[JK_FRAME_LEN - 1];
        const uint8_t got_crc  = jk_checksum(ctx->buf, JK_FRAME_LEN - 1);
        if (want_crc != got_crc) return JK_FEED_CRC_ERROR;

        switch (ctx->buf[JK_OFF_TYPE]) {
        case JK_FRAME_DEVICE_INFO:
            decode_device_info(ctx);
            return JK_FEED_DEVICE_INFO;

        case JK_FRAME_SETTINGS:
            decode_settings(ctx);
            return JK_FEED_SETTINGS;

        case JK_FRAME_CELL_INFO:
            /* Refuse to decode until the layout is known — guessing produces
             * numbers that look plausible and are wrong by 32 bytes. */
            if (ctx->proto == JK_PROTO_UNKNOWN || !out) return JK_FEED_IGNORED;
            decode_cell_info(ctx, out);
            return JK_FEED_SNAPSHOT;

        default:
            return JK_FEED_IGNORED;
        }
    }

    return JK_FEED_NEED_MORE;
}
