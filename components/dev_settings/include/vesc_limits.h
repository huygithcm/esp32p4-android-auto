#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Subset of VESC l_* limits the dashboard exposes in the Settings screen.
 * The real implementation will live in a future vesc_app component and
 * read/write these via COMM_GET_MCCONF / COMM_SET_MCCONF over CAN. The
 * stub below keeps the UI compilable under LV_REALDEVICE without that
 * component yet — read returns "no data", writes return failure so the
 * UI shows a clear error. */
typedef struct {
    float l_current_max;
    float l_in_current_max;
    float l_erpm_max;
} vesc_motor_limits_t;

void                       vesc_limits_init(void);
bool                       vesc_limits_is_valid(void);
const vesc_motor_limits_t *vesc_limits_get(void);
bool                       vesc_limits_request(uint8_t target_id);
bool                       vesc_limits_set_current_max(uint8_t target_id,
                                                       float motor_current,
                                                       float battery_current);
bool                       vesc_limits_set_speed_max(uint8_t target_id,
                                                     float erpm_max);

#ifdef __cplusplus
}
#endif
