/* Host-only wire safety regression. Run from repository root:
 * gcc -std=c11 -Wall -Wextra -I components/vesc_can/include
 * scripts/tests/test_ride_safety.c components/vesc_can/vesc_ride_mode_parse.c
 * components/vesc_can/buffer.c -lm -o test_ride_safety
 * This does not execute Lisp, CAN or motor control.
 */
#include "vesc_can/vesc_ride_mode_wire.h"
#include "vesc_can/vesc_datatypes.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static void packet(uint8_t *b, unsigned id, unsigned state,
                   unsigned profile, unsigned result)
{
    uint8_t good[VRM_SAFETY_MSG_LEN] = {
        COMM_CUSTOM_APP_DATA, VLP_MAGIC0, VLP_MAGIC1, (uint8_t)id,
        VESC_RIDE_SAFETY_VERSION, 0xAB, 0xCD,
        (uint8_t)state, (uint8_t)profile, (uint8_t)result
    };
    memcpy(b, good, sizeof good);
}

static void reject(const uint8_t *b, unsigned len)
{
    vesc_ride_safety_t out, before;
    memset(&out, 0xA5, sizeof out);
    memcpy(&before, &out, sizeof out);
    CHECK(!vesc_ride_mode_parse_safety(b, len, &out));
    CHECK(memcmp(&out, &before, sizeof out) == 0);
}

int main(void)
{
    uint8_t b[64] = {0};
    vesc_ride_safety_t out;
    unsigned ids[] = {VRM_MSG_SAFETY, VRM_MSG_PARK_ACK};
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned state = 0; state <= VESC_RIDE_SAFETY_FAULT; ++state)
            for (unsigned profile = 0; profile < VESC_RIDE_MODE_COUNT; ++profile)
                for (unsigned result = 0; result <= VESC_RIDE_RESULT_STALE_REQUEST; ++result) {
                    packet(b, ids[i], state, profile, result);
                    CHECK(vesc_ride_mode_parse_safety(b, VRM_SAFETY_MSG_LEN, &out));
                    CHECK(out.valid && out.response_seq == 0xABCD);
                    CHECK((unsigned)out.state == state && out.current_profile == profile);
                    CHECK((unsigned)out.result == result && !out.command_pending);
                }
        packet(b, ids[i], 0, 0, 0);
        for (unsigned len = 0; len <= sizeof b; ++len)
            if (len != VRM_SAFETY_MSG_LEN) reject(b, len);
        const unsigned positions[] = {0, 1, 2, 3, 4, 7, 8, 9};
        for (unsigned p = 0; p < sizeof positions / sizeof positions[0]; ++p) {
            packet(b, ids[i], 0, 0, 0);
            b[positions[p]] = 0xFF;
            reject(b, VRM_SAFETY_MSG_LEN);
        }
        packet(b, ids[i], 0, 0, 0);
        b[5] = b[6] = 0;
        reject(b, VRM_SAFETY_MSG_LEN);
    }
    reject(NULL, VRM_SAFETY_MSG_LEN);
    CHECK(!vesc_ride_mode_parse_safety(b, VRM_SAFETY_MSG_LEN, NULL));
    memset(b, 0, sizeof b);
    b[0] = COMM_CUSTOM_APP_DATA; b[1] = VLP_MAGIC0; b[2] = VLP_MAGIC1;
    b[3] = VRM_MSG_STATUS_SEQ; b[4] = 0xFE; b[5] = 0xDC;
    b[15] = 0xFF; /* reverse */
    vesc_ride_status_t status;
    CHECK(vesc_ride_mode_parse_status(b, VRM_STATUS_SEQ_MSG_LEN, &status));
    CHECK(status.response_seq == 0xFEDC && status.direction_state == -1);
    for (unsigned len = 0; len <= sizeof b; ++len)
        if (len != VRM_STATUS_SEQ_MSG_LEN)
            CHECK(!vesc_ride_mode_parse_status(b, len, &status));
    b[4] = b[5] = 0;
    CHECK(!vesc_ride_mode_parse_status(b, VRM_STATUS_SEQ_MSG_LEN, &status));
    CHECK(!vesc_ride_safety_reply_matches(0, 0, 0, 0));
    CHECK(!vesc_ride_safety_reply_matches(2, 1, 0, 1));
    CHECK(vesc_ride_safety_reply_matches(1, 1, 100, 100));
    CHECK(vesc_ride_safety_reply_matches(65535, 65535, 100, 1099));
    CHECK(!vesc_ride_safety_reply_matches(1, 1, 100, 1100));
    CHECK(!vesc_ride_safety_reply_matches(1, 1, 100, 1101));
    CHECK(!vesc_ride_safety_reply_matches(1, 1, 100, 99));
    /* Unsigned millisecond arithmetic must survive uptime counter rollover. */
    CHECK(vesc_ride_safety_reply_matches(1, 1, UINT32_MAX - 500, 498));
    CHECK(!vesc_ride_safety_reply_matches(1, 1, UINT32_MAX - 500, 499));
    CHECK(!vesc_ride_safety_reply_matches(65535, 1, 0, 1));
    printf("Ride safety: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
