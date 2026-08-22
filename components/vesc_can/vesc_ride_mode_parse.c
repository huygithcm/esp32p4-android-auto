/*
    Copyright 2026 Adapted to ESP-IDF for ESP32-P4 (GPL-3.0).

    Pure parsing and validation for the ride-mode protocol, split out from
    vesc_ride_mode.c so the host tests can compile it. Nothing here touches
    FreeRTOS, CAN or any global state: give it bytes, get a struct.

    That split is the point. These are the functions a malformed packet reaches
    first, and they are the ones worth exercising against deliberately broken
    input on a machine where a failure prints instead of resetting a vehicle.
*/

#include "vesc_can/vesc_ride_mode_wire.h"

#include "vesc_can/buffer.h"
#include "vesc_can/vesc_datatypes.h"

#include <string.h>

bool vesc_ride_mode_config_in_range(const vesc_ride_config_t *cfg,
                                    vesc_ride_result_t *why_out)
{
    vesc_ride_result_t why = VESC_RIDE_RESULT_OK;
    if (!cfg) {
        if (why_out) *why_out = VESC_RIDE_RESULT_BAD_LENGTH;
        return false;
    }

    for (unsigned i = 0; i < VESC_RIDE_MODE_COUNT; i++) {
        if (cfg->speed_dkmh[i] < VESC_RIDE_FORWARD_SPEED_MIN_DKMH ||
            cfg->speed_dkmh[i] > VESC_RIDE_FORWARD_SPEED_MAX_DKMH ||
            cfg->current_permille[i] < VESC_RIDE_FORWARD_CURRENT_MIN_PM ||
            cfg->current_permille[i] > VESC_RIDE_FORWARD_CURRENT_MAX_PM) {
            why = VESC_RIDE_RESULT_OUT_OF_RANGE;
            goto done;
        }
    }

    /* Non-decreasing, so mode 3 is never slower than mode 1. A rider who has
     * just set mode 2 above mode 3 should be told, not quietly handed one of
     * the two values back. */
    if (cfg->speed_dkmh[0] > cfg->speed_dkmh[1] ||
        cfg->speed_dkmh[1] > cfg->speed_dkmh[2]) {
        why = VESC_RIDE_RESULT_ORDER_INVALID;
        goto done;
    }

    /* Reverse limits are checked whether or not reverse is enabled: a disabled
     * config still gets stored, and storing nonsense means the day it is
     * switched on is the day it misbehaves. */
    if (cfg->reverse_speed_dkmh < VESC_RIDE_REVERSE_SPEED_MIN_DKMH ||
        cfg->reverse_speed_dkmh > VESC_RIDE_REVERSE_SPEED_MAX_DKMH ||
        cfg->reverse_current_dA < VESC_RIDE_REVERSE_CURRENT_MIN_DA ||
        cfg->reverse_current_dA > VESC_RIDE_REVERSE_CURRENT_MAX_DA) {
        why = VESC_RIDE_RESULT_OUT_OF_RANGE;
    }

done:
    if (why_out) *why_out = why;
    return why == VESC_RIDE_RESULT_OK;
}


bool vesc_ride_mode_parse_config(const uint8_t *data, unsigned int len,
                                 vesc_ride_config_t *out)
{
    if (!data || !out) return false;
    /* Length first, always. Every read below is a fixed offset and there is no
     * count field that could make a short packet look complete. */
    if (len < VRM_CONFIG_MSG_LEN) return false;
    if (data[0] != COMM_CUSTOM_APP_DATA) return false;
    if (data[1] != VLP_MAGIC0 || data[2] != VLP_MAGIC1) return false;
    if (data[3] != VRM_MSG_CONFIG) return false;

    int32_t ind = 4;
    const uint16_t seq    = buffer_get_uint16(data, &ind);
    const uint8_t  result = data[ind++];
    const uint8_t  fmt    = data[ind++];

    vesc_ride_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.response_seq   = seq;
    cfg.format_version = fmt;

    /* An unknown format is not a config we can read: the fields below may have
     * moved. Report it rather than decode anyway -- valid stays false so the
     * screen shows nothing, but the sequence number is still trustworthy, so a
     * Save waiting on this reply gets an answer instead of a timeout. */
    if (fmt != VESC_RIDE_CONFIG_FORMAT_VERSION) {
        cfg.valid       = false;
        cfg.last_result = VESC_RIDE_RESULT_BAD_VERSION;
        *out = cfg;
        return true;
    }

    cfg.config_revision = buffer_get_uint16(data, &ind);
    for (unsigned i = 0; i < VESC_RIDE_MODE_COUNT; i++) {
        cfg.speed_dkmh[i]       = buffer_get_uint16(data, &ind);
        cfg.current_permille[i] = buffer_get_uint16(data, &ind);
    }
    cfg.reverse_enabled    = data[ind++] != 0;
    cfg.reverse_speed_dkmh = buffer_get_uint16(data, &ind);
    cfg.reverse_current_dA = buffer_get_uint16(data, &ind);
    cfg.persist_pending    = data[ind++] != 0;
    cfg.last_result        = (result <= VESC_RIDE_RESULT_UNSUPPORTED_HARDWARE)
                             ? (vesc_ride_result_t)result
                             : VESC_RIDE_RESULT_OUT_OF_RANGE;
    /* valid means "these numbers are what the vehicle holds". A refusal still
     * carries the config in force -- that is why it is echoed -- so the editor
     * can snap back to reality instead of leaving rejected numbers on screen. */
    cfg.valid = true;

    *out = cfg;
    return true;
}


bool vesc_ride_mode_parse_status(const uint8_t *data, unsigned int len,
                                 vesc_ride_status_t *out)
{
    if (!data || !out) return false;
    if (len < VRM_STATUS_MSG_LEN) return false;
    if (data[0] != COMM_CUSTOM_APP_DATA) return false;
    if (data[1] != VLP_MAGIC0 || data[2] != VLP_MAGIC1) return false;
    if (data[3] != VRM_MSG_STATUS) return false;

    int32_t ind = 4;
    vesc_ride_status_t st;
    memset(&st, 0, sizeof st);
    st.config_revision   = buffer_get_uint16(data, &ind);
    st.current_profile   = data[ind++];
    st.active_speed_dkmh = buffer_get_uint16(data, &ind);
    st.direction_state   = (int8_t)data[ind++];
    st.reverse_button    = data[ind++] != 0;
    st.reverse_armed     = data[ind++] != 0;
    st.persist_pending   = data[ind++] != 0;
    st.fault_reason      = data[ind++];

    /* A profile outside 0..2 means the two sides disagree about the protocol;
     * publishing it would index the screen's name array off its end. Same for
     * a direction that is not one of the three defined states. */
    if (st.current_profile >= VESC_RIDE_MODE_COUNT) return false;
    if (st.direction_state < -1 || st.direction_state > 1) return false;

    st.valid = true;
    *out = st;
    return true;
}
