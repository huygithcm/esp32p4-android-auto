/*
 * Host test for the JK BMS BLE parser (components/bms/bms_jk.c).
 *
 * The parser is the part of the BMS path that cannot be verified on the bench
 * without the real pack, and the part where a mistake is invisible: a 32-byte
 * layout shift still yields voltages and currents in a believable range. So it
 * is tested here against synthesised frames whose every field is known.
 *
 * Build & run:
 *   cc -std=c11 -I components/bms/include \
 *      tools/test/test_bms_jk.c components/bms/bms_jk.c -o /tmp/test_bms_jk
 *   /tmp/test_bms_jk
 *
 * bms_model.c is deliberately NOT linked: it pulls in esp_timer and FreeRTOS.
 * The parser never calls it, which is the point of keeping the two apart.
 */

#include "bms/bms_jk.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

#define CHECK(cond, fmt, ...)                                            \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("  FAIL %s:%d: " fmt "\n", __FILE__, __LINE__,        \
                   ##__VA_ARGS__);                                       \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static void put_u16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static void put_i32(uint8_t *p, int32_t v) { put_u32(p, (uint32_t)v); }

static void seal(uint8_t *f)
{
    uint8_t sum = 0;
    for (int i = 0; i < JK_FRAME_LEN - 1; i++) sum = (uint8_t)(sum + f[i]);
    f[JK_FRAME_LEN - 1] = sum;
}

/* A 24S-layout cell-info frame with 4 populated cells. */
static void make_cell_frame(uint8_t *f, int shift, int ncell_populated)
{
    memset(f, 0, JK_FRAME_LEN);
    f[0] = 0x55; f[1] = 0xAA; f[2] = 0xEB; f[3] = 0x90;
    f[4] = JK_FRAME_CELL_INFO;

    /* cells: 3200, 3300, 3400, 3350 mV */
    static const uint16_t mv[4] = { 3200, 3300, 3400, 3350 };
    for (int i = 0; i < ncell_populated; i++) put_u16(&f[6 + i * 2], mv[i]);

    put_u32(&f[118 + shift], 40000);        /* 40.000 V pack            */
    put_i32(&f[126 + shift], -12500);       /* JK: -12.5 A = discharging */
    put_u16(&f[130 + shift], (uint16_t)250);/* temp1 25.0 C             */
    put_u16(&f[132 + shift], (uint16_t)260);/* temp2 26.0 C             */
    f[140 + shift] = 1;                     /* balancing while charging */
    f[141 + shift] = 77;                    /* SoC 77 %                 */
    put_u32(&f[142 + shift], 15000);        /* 15.000 Ah remaining      */
    put_u32(&f[146 + shift], 20000);        /* 20.000 Ah nominal        */
    put_u32(&f[150 + shift], 42);           /* 42 cycles                */
    put_u32(&f[154 + shift], 123456);       /* 123.456 Ah cycle capacity */
    f[158 + shift] = 96;                    /* SoH 96 %                 */
    f[166 + shift] = 1;                     /* charge MOS on            */
    f[167 + shift] = 0;                     /* discharge MOS off        */
    put_u16(&f[138 + shift], (uint16_t)450);/* balance current 0.450 A  */
    f[183 + shift] = 1;                     /* heater on                */
    put_u16(&f[204 + shift], (uint16_t)1200); /* heater 1.200 A         */
    if (shift) put_u16(&f[186 + shift], 90); /* 32S only: 90 s timer    */

    /* Per-cell resistance sits INSIDE the cell region, so its base moves by
     * only half the shift: 64 on 24S, 80 on 32S. */
    {
        const int res_base = 64 + shift / 2;
        static const uint16_t mohm[4] = { 3, 5, 4, 6 };
        for (int i = 0; i < ncell_populated; i++)
            put_u16(&f[res_base + i * 2], mohm[i]);
    }

    if (shift) put_u16(&f[112], (uint16_t)310);        /* 32S MOS temp  */
    else       put_u16(&f[134], (uint16_t)310);        /* 24S MOS temp  */

    /* Alarm mask: u32 at 134+shift on 32S, u16 at 136 on 24S — the same
     * region 24S uses for its MOS temperature, hence the split above. */
    if (shift) put_u32(&f[134 + shift], 0x21);
    else       put_u16(&f[136], 0x21);

    seal(f);
}

static void check_common(const bms_snapshot_t *s, const char *tag)
{
    printf("[%s]\n", tag);
    CHECK(s->pack_mv == 40000, "pack_mv=%d want 40000", (int)s->pack_mv);
    /* JK says -12500 (discharging); the model's convention flips it. */
    CHECK(s->pack_current_ma == 12500,
          "current=%d want +12500 (sign must be inverted)", (int)s->pack_current_ma);
    CHECK(s->soc_permille == 770, "soc=%u want 770", s->soc_permille);
    CHECK(s->remaining_mah == 15000, "remaining=%d", (int)s->remaining_mah);
    CHECK(s->nominal_mah == 20000, "nominal=%d", (int)s->nominal_mah);
    CHECK(s->cycle_count == 42, "cycles=%u", s->cycle_count);
    CHECK(s->cell_count == 4, "cell_count=%u want 4", s->cell_count);
    CHECK(s->cell_mv[0] == 3200 && s->cell_mv[2] == 3400, "cell voltages wrong");
    CHECK(s->cell_min_mv == 3200 && s->cell_max_mv == 3400, "min/max wrong");
    CHECK(s->cell_delta_mv == 200, "delta=%u want 200", s->cell_delta_mv);
    CHECK(s->cell_min_idx == 0 && s->cell_max_idx == 2, "min/max index wrong");
    CHECK(s->cell_valid_mask == 0xF, "valid mask=%x want f", s->cell_valid_mask);
    CHECK(s->temp_deci_c[0] == 250 && s->temp_deci_c[1] == 260, "temps wrong");
    CHECK(s->mos_temp_deci_c == 310, "mos temp=%d want 310", s->mos_temp_deci_c);
    CHECK(s->chg_mos_on && !s->dsg_mos_on, "mosfet flags wrong");
    CHECK(s->balancing, "balancing flag wrong");
    CHECK(s->soh_permille == 960, "soh=%u want 960", s->soh_permille);
    CHECK(s->valid_mask & BMS_V_SOH, "SoH must be valid — it is at 158+shift");
    CHECK(s->cycle_capacity_mah == 123456, "cycle cap=%d", (int)s->cycle_capacity_mah);
    CHECK(s->balance_current_ma == 450, "bal current=%d", (int)s->balance_current_ma);
    CHECK(s->heater_on && s->heater_current_ma == 1200, "heater wrong");
    CHECK((s->valid_mask & BMS_V_ALARMS) && s->alarm_raw == 0x21,
          "alarm mask wrong: valid=%d raw=0x%x",
          !!(s->valid_mask & BMS_V_ALARMS), (unsigned)s->alarm_raw);
    CHECK(s->valid_mask & BMS_V_WIRE_RES, "wire resistance must be decoded");
    CHECK(s->wire_res_mohm[0] == 3 && s->wire_res_mohm[1] == 5 &&
          s->wire_res_mohm[3] == 6, "wire res wrong: %u %u %u",
          s->wire_res_mohm[0], s->wire_res_mohm[1], s->wire_res_mohm[3]);
    CHECK(s->wire_res_valid_mask == 0xF, "wire mask=%x want f",
          s->wire_res_valid_mask);
    CHECK(s->driver_id == BMS_DRIVER_JK_BLE, "driver_id wrong");
}

/* Feed a frame in chunks to exercise reassembly. */
static jk_feed_result_t feed_chunked(jk_ctx_t *ctx, const uint8_t *f, size_t n,
                                     size_t chunk, bms_snapshot_t *out)
{
    jk_feed_result_t last = JK_FEED_NEED_MORE;
    for (size_t i = 0; i < n; i += chunk) {
        const size_t take = (n - i < chunk) ? (n - i) : chunk;
        last = jk_feed(ctx, f + i, take, out);
    }
    return last;
}

int main(void)
{
    uint8_t f[JK_FRAME_LEN];
    bms_snapshot_t s;
    jk_ctx_t ctx;

    /* ---- 24S layout, delivered 20 bytes at a time (worst-case MTU) ------ */
    jk_init(&ctx);
    jk_set_proto(&ctx, JK_PROTO_02_24S);
    make_cell_frame(f, 0, 4);
    CHECK(feed_chunked(&ctx, f, sizeof f, 20, &s) == JK_FEED_SNAPSHOT,
          "24S frame did not decode");
    check_common(&s, "JK02_24S, 20-byte fragments");

    /* ---- 32S layout, one shot ------------------------------------------- */
    jk_init(&ctx);
    jk_set_proto(&ctx, JK_PROTO_02_32S);
    make_cell_frame(f, 32, 4);
    CHECK(jk_feed(&ctx, f, sizeof f, &s) == JK_FEED_SNAPSHOT,
          "32S frame did not decode");
    check_common(&s, "JK02_32S, single fragment");

    /* ---- a corrupted checksum must be rejected, not decoded ------------- */
    printf("[corrupt checksum]\n");
    jk_init(&ctx);
    jk_set_proto(&ctx, JK_PROTO_02_24S);
    make_cell_frame(f, 0, 4);
    f[JK_FRAME_LEN - 1] ^= 0xFF;
    CHECK(jk_feed(&ctx, f, sizeof f, &s) == JK_FEED_CRC_ERROR,
          "bad checksum was not caught");

    /* ---- unknown layout must refuse rather than guess ------------------- */
    printf("[layout unknown]\n");
    jk_init(&ctx);                       /* proto left UNKNOWN */
    make_cell_frame(f, 0, 4);
    CHECK(jk_feed(&ctx, f, sizeof f, &s) == JK_FEED_IGNORED,
          "decoded a cell frame without knowing the layout");

    /* ---- resync: garbage, then a truncated frame, then a good one ------- */
    printf("[resync after a lost fragment]\n");
    jk_init(&ctx);
    jk_set_proto(&ctx, JK_PROTO_02_24S);
    const uint8_t junk[] = { 0x00, 0x55, 0x11, 0x55, 0xAA, 0x22 };
    jk_feed(&ctx, junk, sizeof junk, &s);
    make_cell_frame(f, 0, 4);
    jk_feed(&ctx, f, 150, &s);           /* half a frame, then it vanishes */
    CHECK(jk_feed(&ctx, f, sizeof f, &s) == JK_FEED_SNAPSHOT,
          "parser did not resynchronise on the next preamble");

    /* ---- timers exist only on 32S --------------------------------------- */
    printf("[timers are 32S-only]\n");
    jk_init(&ctx);
    jk_set_proto(&ctx, JK_PROTO_02_24S);
    make_cell_frame(f, 0, 4);
    jk_feed(&ctx, f, sizeof f, &s);
    CHECK((s.valid_mask & BMS_V_TIMERS) == 0,
          "24S must not publish a timer — those bytes mean something else");
    jk_init(&ctx);
    jk_set_proto(&ctx, JK_PROTO_02_32S);
    make_cell_frame(f, 32, 4);
    jk_feed(&ctx, f, sizeof f, &s);
    CHECK((s.valid_mask & BMS_V_TIMERS) && s.emergency_timer_s == 90,
          "32S timer wrong: %u", s.emergency_timer_s);

    /* ---- the resistance base moves by HALF the shift on 32S --------------
     * Inside the cell region the 32S delta is 16; only past the cell block
     * does it become 32. Reading the resistances at the full shift would look
     * perfectly reasonable and be 16 bytes wrong, so plant a decoy at that
     * position and require the decoder does not pick it up. */
    printf("[resistance base uses half the shift on 32S]\n");
    jk_init(&ctx);
    jk_set_proto(&ctx, JK_PROTO_02_32S);
    make_cell_frame(f, 32, 4);
    put_u16(&f[64 + 32], 999);          /* decoy at the full-shift offset */
    seal(f);
    CHECK(jk_feed(&ctx, f, sizeof f, &s) == JK_FEED_SNAPSHOT, "32S frame");
    CHECK(s.wire_res_mohm[0] == 3,
          "read resistance from the full-shift offset: got %u, want 3",
          s.wire_res_mohm[0]);

    /* An unpopulated cell must not gain a resistance reading either. */
    printf("[resistance follows the cell mask]\n");
    jk_init(&ctx);
    jk_set_proto(&ctx, JK_PROTO_02_24S);
    make_cell_frame(f, 0, 2);           /* only two cells populated */
    CHECK(jk_feed(&ctx, f, sizeof f, &s) == JK_FEED_SNAPSHOT, "cell frame");
    CHECK(s.wire_res_valid_mask == 0x3,
          "wire mask must match the cell mask, got 0x%x", s.wire_res_valid_mask);

    /* ---- command framing ------------------------------------------------ */
    printf("[command frame]\n");
    uint8_t cmd[JK_CMD_LEN];
    CHECK(jk_build_cmd(cmd, sizeof cmd, JK_CMD_CELL_INFO, 0) == JK_CMD_LEN,
          "jk_build_cmd length");
    /* Commands use the response header BYTE-SWAPPED. Sending 55 AA EB 90 here
     * is accepted by the stack, ignored by the BMS, and looks exactly like a
     * dead pack — so pin it down. */
    CHECK(cmd[0] == 0xAA && cmd[1] == 0x55 && cmd[2] == 0x90 && cmd[3] == 0xEB,
          "command header must be AA 55 90 EB, got %02X %02X %02X %02X",
          cmd[0], cmd[1], cmd[2], cmd[3]);
    CHECK(!(cmd[0] == 0x55 && cmd[1] == 0xAA),
          "command must NOT reuse the response preamble");
    CHECK(cmd[4] == JK_CMD_CELL_INFO, "command byte wrong");
    uint8_t sum = 0;
    for (int i = 0; i < JK_CMD_LEN - 1; i++) sum = (uint8_t)(sum + cmd[i]);
    CHECK(cmd[JK_CMD_LEN - 1] == sum, "command checksum wrong");
    CHECK(jk_build_cmd(cmd, 4, JK_CMD_CELL_INFO, 0) == 0,
          "jk_build_cmd must refuse a short buffer");

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
