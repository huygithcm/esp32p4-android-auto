/*
    Copyright 2026 ESP32-P4 Android Auto / VESC Display (GPL-3.0).

    Dual-head helper. A dual-motor board is two VESC nodes on one CAN bus.
    The display polls the primary head (target_vesc_id) via SETUP_SELECTIVE —
    which the VESC firmware already aggregates (Ah/Wh/currents) across all CAN
    nodes whose STATUS is <100 ms fresh. Temperatures, however, are NOT
    aggregated: each node reports only its own. So the second head's temps and
    liveness are read passively from its periodic CAN STATUS_4 broadcast.

    Requires the user to enable "Send status over CAN" on the second head.
    All functions return false when the second head is disabled or its STATUS
    is stale, so callers degrade to single-head behaviour automatically.
*/

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fills *temp_fet / *temp_motor (either may be NULL) from the second head's
 * latest STATUS_4 and returns true, iff the second head is enabled in
 * settings and its STATUS_4 is fresh. Otherwise returns false and leaves the
 * outputs untouched. */
bool vesc_head2_get_temps(float *temp_fet, float *temp_motor);

/* True iff the second head is enabled and currently broadcasting fresh STATUS.
 * Used by the connection-state logic ("ESC NOT CONNECTED" if any head silent). */
bool vesc_head2_is_fresh(void);

#ifdef __cplusplus
}
#endif
