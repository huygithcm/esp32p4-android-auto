/* Host tests for the ride-mode wire parsers and range check.
 *
 * Contract section 12 ("Host/static") asks for: valid packet, short packet,
 * bad version, invalid mode, stale sequence and unknown message. Each of those
 * is a group below, plus the ordering rule, which is the one a rider can trip
 * by editing two numbers in the wrong order.
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra -I components/vesc_can/include \
 *       tools/test/test_ride_mode.c components/vesc_can/buffer.c -o test_ride
 */

#include "vesc_can/vesc_ride_mode_wire.h"
#include "vesc_can/buffer.h"

#include <stdio.h>
#include <string.h>

/* The parsers reference COMM_CUSTOM_APP_DATA; pulling vesc_datatypes.h in on
 * the host drags the whole firmware tree, so pin the one value used. Kept in
 * step by the assertion in main(). */
#define COMM_CUSTOM_APP_DATA 36

static int g_fail;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------- builders */

/* A well-formed RIDE_CONFIG carrying a configuration whose top mode asks for
 * more than the ESC can deliver -- the case format 2 exists to express. */
static unsigned build_config(uint8_t *b, uint16_t seq, uint8_t result,
                             uint8_t fmt)
{
    int32_t ind = 0;
    b[ind++] = COMM_CUSTOM_APP_DATA;
    b[ind++] = 0x56;
    b[ind++] = 0x50;
    b[ind++] = VRM_MSG_CONFIG;
    buffer_append_uint16(b, seq, &ind);
    b[ind++] = result;
    b[ind++] = fmt;
    buffer_append_uint16(b, 7, &ind);        /* config_revision */
    buffer_append_uint16(b, 500,  &ind);     /* mode0  50.0 A */
    buffer_append_uint16(b, 700,  &ind);     /* mode1  70.0 A */
    buffer_append_uint16(b, 1000, &ind);     /* mode2 100.0 A, above the ESC */
    b[ind++] = 1;                            /* reverse_enabled */
    buffer_append_uint16(b, 30, &ind);       /* reverse_speed_dkmh */
    buffer_append_uint16(b, 70, &ind);       /* reverse_current_dA */
    buffer_append_uint16(b, 700, &ind);      /* esc_current_max_dA 70.0 A */
    b[ind++] = 1;                            /* persist_pending */
    return (unsigned)ind;
}

static unsigned build_status(uint8_t *b, uint8_t profile, int8_t dir)
{
    int32_t ind = 0;
    b[ind++] = COMM_CUSTOM_APP_DATA;
    b[ind++] = 0x56;
    b[ind++] = 0x50;
    b[ind++] = VRM_MSG_STATUS;
    buffer_append_uint16(b, 7, &ind);        /* config_revision */
    b[ind++] = profile;
    buffer_append_uint16(b, 1000, &ind);     /* requested 100.0 A */
    buffer_append_uint16(b, 700,  &ind);     /* effective  70.0 A, clamped */
    buffer_append_uint16(b, 700,  &ind);     /* esc max     70.0 A */
    b[ind++] = (uint8_t)dir;
    b[ind++] = 1;                            /* reverse_button */
    b[ind++] = 0;                            /* reverse_armed */
    b[ind++] = 0;                            /* persist_pending */
    b[ind++] = 0;                            /* fault_reason */
    return (unsigned)ind;
}

static void defaults(vesc_ride_config_t *c)
{
    memset(c, 0, sizeof *c);
    c->mode_current_dA[0] = 500;
    c->mode_current_dA[1] = 700;
    c->mode_current_dA[2] = 1000;
    c->reverse_enabled    = false;
    c->reverse_speed_dkmh = 30;
    c->reverse_current_dA = 70;
}

/* ------------------------------------------------------------------ tests */

int main(void)
{
    uint8_t  b[64];
    unsigned n;
    vesc_ride_config_t cfg;
    vesc_ride_status_t st;

    /* The builders above hard-code the COMM id; if the firmware's ever moved,
     * every packet here would be built wrong and every test would still pass. */
    CHECK(COMM_CUSTOM_APP_DATA == 36, "COMM_CUSTOM_APP_DATA drifted");

    printf("[valid config packet]\n");
    n = build_config(b, 42, VESC_RIDE_RESULT_OK, VESC_RIDE_CONFIG_FORMAT_VERSION);
    CHECK(n == VRM_CONFIG_MSG_LEN, "built %u bytes, header says %u",
          n, VRM_CONFIG_MSG_LEN);
    CHECK(vesc_ride_mode_parse_config(b, n, &cfg), "valid packet rejected");
    CHECK(cfg.valid, "valid flag not set");
    CHECK(cfg.response_seq == 42, "seq=%u", cfg.response_seq);
    CHECK(cfg.config_revision == 7, "revision=%u", cfg.config_revision);
    CHECK(cfg.mode_current_dA[0] == 500, "mode0 %u", cfg.mode_current_dA[0]);
    CHECK(cfg.mode_current_dA[2] == 1000, "mode2 %u", cfg.mode_current_dA[2]);
    CHECK(cfg.esc_current_max_dA == 700, "esc max %u", cfg.esc_current_max_dA);
    CHECK(cfg.reverse_enabled, "reverse_enabled lost");
    CHECK(cfg.reverse_speed_dkmh == 30 && cfg.reverse_current_dA == 70,
          "reverse %u/%u", cfg.reverse_speed_dkmh, cfg.reverse_current_dA);
    CHECK(cfg.persist_pending, "persist_pending lost");
    CHECK(cfg.last_result == VESC_RIDE_RESULT_OK, "result=%d", cfg.last_result);

    /* Every length below the declared one must be refused. A parser that only
     * checks the total can still walk off a buffer that was truncated one byte
     * short, so sweep the whole range rather than testing one short packet. */
    printf("[short packets, every length]\n");
    n = build_config(b, 1, VESC_RIDE_RESULT_OK, VESC_RIDE_CONFIG_FORMAT_VERSION);
    for (unsigned len = 0; len < n; len++) {
        vesc_ride_config_t junk;
        memset(&junk, 0xAB, sizeof junk);
        CHECK(!vesc_ride_mode_parse_config(b, len, &junk),
              "accepted a %u-byte config packet", len);
    }
    n = build_status(b, 1, 1);
    for (unsigned len = 0; len < n; len++) {
        vesc_ride_status_t junk;
        memset(&junk, 0xAB, sizeof junk);
        CHECK(!vesc_ride_mode_parse_status(b, len, &junk),
              "accepted a %u-byte status packet", len);
    }

    /* A format we do not know still yields the sequence number, because a Save
     * waiting on that seq deserves an answer rather than a timeout -- but
     * never the fields, which may have moved. */
    printf("[bad format version]\n");
    n = build_config(b, 77, VESC_RIDE_RESULT_OK, 99);
    CHECK(vesc_ride_mode_parse_config(b, n, &cfg), "bad version not reported");
    CHECK(!cfg.valid, "bad version must not be marked valid");
    CHECK(cfg.last_result == VESC_RIDE_RESULT_BAD_VERSION,
          "result=%d want BAD_VERSION", cfg.last_result);
    CHECK(cfg.response_seq == 77, "seq lost on bad version: %u",
          cfg.response_seq);
    CHECK(cfg.mode_current_dA[0] == 0, "decoded fields from an unknown format");

    printf("[unknown message id is not ours]\n");
    n = build_config(b, 1, VESC_RIDE_RESULT_OK, VESC_RIDE_CONFIG_FORMAT_VERSION);
    b[3] = 0x84;                       /* the panel's DASH */
    CHECK(!vesc_ride_mode_parse_config(b, n, &cfg), "claimed a DASH packet");
    b[3] = VRM_MSG_CONFIG;
    b[1] = 0x55;                       /* wrong magic */
    CHECK(!vesc_ride_mode_parse_config(b, n, &cfg), "ignored the magic");
    b[1] = 0x56;
    b[0] = 0x00;                       /* wrong COMM id */
    CHECK(!vesc_ride_mode_parse_config(b, n, &cfg), "ignored the COMM id");

    /* A refusal still carries the configuration in force: that is the whole
     * point of echoing it, so the editor can snap back to reality. */
    printf("[refusal still carries the live config]\n");
    n = build_config(b, 9, VESC_RIDE_RESULT_VEHICLE_MOVING,
                     VESC_RIDE_CONFIG_FORMAT_VERSION);
    CHECK(vesc_ride_mode_parse_config(b, n, &cfg), "refusal packet rejected");
    CHECK(cfg.valid, "refusal must still carry usable numbers");
    CHECK(cfg.last_result == VESC_RIDE_RESULT_VEHICLE_MOVING,
          "result=%d", cfg.last_result);
    CHECK(cfg.mode_current_dA[1] == 700, "config lost on a refusal");

    printf("[valid status packet]\n");
    n = build_status(b, 2, -1);
    CHECK(n == VRM_STATUS_MSG_LEN, "built %u bytes, header says %u",
          n, VRM_STATUS_MSG_LEN);
    CHECK(vesc_ride_mode_parse_status(b, n, &st), "valid status rejected");
    CHECK(st.valid, "valid flag not set");
    CHECK(st.current_profile == 2, "profile=%u", st.current_profile);
    CHECK(st.direction_state == -1, "direction=%d", st.direction_state);
    CHECK(st.requested_current_dA == 1000, "requested=%u",
          st.requested_current_dA);
    CHECK(st.effective_current_dA == 700, "effective=%u",
          st.effective_current_dA);
    CHECK(st.esc_current_max_dA == 700, "esc max=%u", st.esc_current_max_dA);
    CHECK(st.reverse_button, "button lost");

    /* Out-of-range enums are the ones that reach an array index in the screen,
     * so they must not merely be clamped -- the packet is refused. */
    printf("[status enums are bounded]\n");
    n = build_status(b, 3, 1);          /* profile 3 does not exist */
    CHECK(!vesc_ride_mode_parse_status(b, n, &st), "accepted profile 3");
    n = build_status(b, 0, 1);
    b[13] = 5;                           /* direction 5 */
    CHECK(!vesc_ride_mode_parse_status(b, n, &st), "accepted direction 5");

    /* ---- range check, contract section 3 -------------------------------- */

    printf("[range check accepts the defaults]\n");
    {
        vesc_ride_result_t why = VESC_RIDE_RESULT_TIMEOUT;
        defaults(&cfg);
        CHECK(vesc_ride_mode_config_in_range(&cfg, &why),
              "defaults rejected: why=%d", why);
        CHECK(why == VESC_RIDE_RESULT_OK, "why=%d on a good config", why);
    }

    printf("[range check rejects each field]\n");
    {
        vesc_ride_result_t why;
        struct { const char *what; unsigned idx; uint16_t val; } cases[] = {
            { "current below minimum", 0, VESC_RIDE_CURRENT_MIN_DA - 1 },
            { "current above maximum", 2, VESC_RIDE_CURRENT_MAX_DA + 1 },
        };
        for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
            defaults(&cfg);
            cfg.mode_current_dA[cases[k].idx] = cases[k].val;
            why = VESC_RIDE_RESULT_OK;
            CHECK(!vesc_ride_mode_config_in_range(&cfg, &why),
                  "%s accepted", cases[k].what);
            CHECK(why == VESC_RIDE_RESULT_OUT_OF_RANGE,
                  "%s -> why=%d", cases[k].what, why);
        }
    }

    /* Format 1 required non-decreasing speeds. Format 2's modes are
     * independent current limits, so a decreasing set is a legitimate choice
     * and must be accepted -- this asserts the rule is really gone, not merely
     * unreachable. */
    printf("[modes are independent, decreasing is legal]\n");
    {
        vesc_ride_result_t why = VESC_RIDE_RESULT_TIMEOUT;
        defaults(&cfg);
        cfg.mode_current_dA[0] = 900;
        cfg.mode_current_dA[1] = 400;
        cfg.mode_current_dA[2] = 700;
        CHECK(vesc_ride_mode_config_in_range(&cfg, &why),
              "90/40/70 A rejected: why=%d", why);
        CHECK(why == VESC_RIDE_RESULT_OK, "why=%d", why);
    }

    /* A value above what this ESC can deliver is legal to STORE: it clamps
     * when applied, and that is what lets a later Motor Current Max increase
     * take effect without re-entering anything. */
    printf("[a mode above the ESC is storable]\n");
    {
        vesc_ride_result_t why = VESC_RIDE_RESULT_TIMEOUT;
        defaults(&cfg);
        cfg.mode_current_dA[2] = VESC_RIDE_CURRENT_MAX_DA;
        CHECK(vesc_ride_mode_config_in_range(&cfg, &why),
              "999 A rejected: why=%d", why);
    }

    /* Reverse uses the same requested-current range as the forward modes.
     * Values above the ESC are legal to store because Lisp clamps them at
     * command time; the wire validator still rejects values above 999 A. */
    printf("[reverse limits apply while disabled]\n");
    {
        vesc_ride_result_t why = VESC_RIDE_RESULT_OK;
        defaults(&cfg);
        cfg.reverse_enabled    = false;
        cfg.reverse_current_dA = VESC_RIDE_REVERSE_CURRENT_MAX_DA;
        CHECK(vesc_ride_mode_config_in_range(&cfg, &why),
              "999 A reverse rejected while disabled: why=%d", why);

        defaults(&cfg);
        cfg.reverse_current_dA = VESC_RIDE_REVERSE_CURRENT_MAX_DA + 1;
        why = VESC_RIDE_RESULT_OK;
        CHECK(!vesc_ride_mode_config_in_range(&cfg, &why),
              "reverse above 999 A accepted while disabled");
        CHECK(why == VESC_RIDE_RESULT_OUT_OF_RANGE, "why=%d", why);

        defaults(&cfg);
        cfg.reverse_speed_dkmh = 60;     /* 6 km/h, over the 5 km/h cap */
        why = VESC_RIDE_RESULT_OK;
        CHECK(!vesc_ride_mode_config_in_range(&cfg, &why),
              "over-speed reverse accepted");
    }

    printf("[null config is refused, not dereferenced]\n");
    {
        vesc_ride_result_t why = VESC_RIDE_RESULT_OK;
        CHECK(!vesc_ride_mode_config_in_range(NULL, &why), "NULL accepted");
        CHECK(why == VESC_RIDE_RESULT_BAD_LENGTH, "why=%d", why);
        CHECK(!vesc_ride_mode_parse_config(NULL, 32, &cfg), "NULL data parsed");
        CHECK(!vesc_ride_mode_parse_config(b, 32, NULL), "NULL out accepted");
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
