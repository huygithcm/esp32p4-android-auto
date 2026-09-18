#pragma once

#include "vesc_can/vesc_ride_mode.h"

/* Unknown/stale/interlocked is never an assertion that reverse is ready. */
static inline char ride_gear_symbol(const vesc_ride_safety_t *state)
{
    if (!state || !state->valid) return '-';
    switch (state->state) {
    case VESC_RIDE_SAFETY_PARK: return 'P';
    case VESC_RIDE_SAFETY_FORWARD:
        return state->current_profile < VESC_RIDE_MODE_COUNT ?
            (char)('1' + state->current_profile) : '-';
    case VESC_RIDE_SAFETY_REVERSE_READY:
    case VESC_RIDE_SAFETY_REVERSE_ACTIVE: return 'R';
    default: return '-';
    }
}
