/* Public, non-blocking snapshot API for the ride-mode settings screen.
 *
 * VESC Lisp remains the source of truth. The LVGL task only stages edits,
 * queues a whole-config request and observes replies through these snapshots.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VESC_RIDE_MODE_COUNT                    3u
#define VESC_RIDE_CONFIG_FORMAT_VERSION         1u

#define VESC_RIDE_FORWARD_SPEED_MIN_DKMH       10u
#define VESC_RIDE_FORWARD_SPEED_MAX_DKMH     1500u
#define VESC_RIDE_FORWARD_CURRENT_MIN_PM       100u
#define VESC_RIDE_FORWARD_CURRENT_MAX_PM      1000u
#define VESC_RIDE_REVERSE_SPEED_MIN_DKMH       10u
#define VESC_RIDE_REVERSE_SPEED_MAX_DKMH       50u
#define VESC_RIDE_REVERSE_CURRENT_MIN_DA       10u
#define VESC_RIDE_REVERSE_CURRENT_MAX_DA      140u

typedef enum {
    VESC_RIDE_RESULT_OK = 0,
    VESC_RIDE_RESULT_BAD_LENGTH,
    VESC_RIDE_RESULT_BAD_VERSION,
    VESC_RIDE_RESULT_OUT_OF_RANGE,
    VESC_RIDE_RESULT_ORDER_INVALID,
    VESC_RIDE_RESULT_VEHICLE_MOVING,
    VESC_RIDE_RESULT_THROTTLE_NOT_RELEASED,
    VESC_RIDE_RESULT_REVERSE_ACTIVE,
    VESC_RIDE_RESULT_STORAGE_ERROR,
    VESC_RIDE_RESULT_UNSUPPORTED_HARDWARE,
    VESC_RIDE_RESULT_TIMEOUT,
} vesc_ride_result_t;

typedef struct {
    uint32_t           epoch;
    bool               valid;
    uint16_t           response_seq;
    vesc_ride_result_t last_result;
    uint8_t            format_version;
    uint16_t           config_revision;
    uint16_t           speed_dkmh[VESC_RIDE_MODE_COUNT];
    uint16_t           current_permille[VESC_RIDE_MODE_COUNT];
    bool               reverse_enabled;
    uint16_t           reverse_speed_dkmh;
    uint16_t           reverse_current_dA;
    bool               persist_pending;
} vesc_ride_config_t;

typedef struct {
    uint32_t epoch;
    bool     valid;
    uint16_t config_revision;
    uint8_t  current_profile;
    uint16_t active_speed_dkmh;
    int8_t   direction_state; /* 1 forward, 0 interlock/coast, -1 reverse */
    bool     reverse_button;
    bool     reverse_armed;
    bool     persist_pending;
    uint8_t  fault_reason;
} vesc_ride_status_t;

/* Backend lifecycle/transport hooks. These are called from the existing VESC
 * CAN owner task; the UI must only use the non-blocking calls below. */
void vesc_ride_mode_init(uint8_t target_vesc_id);
void vesc_ride_mode_set_target(uint8_t target_vesc_id);
void vesc_ride_mode_polls_pause(bool paused);
void vesc_ride_mode_poll_loop(void);
void vesc_ride_mode_process_response(const uint8_t *data, unsigned int len);

bool vesc_ride_mode_get_config(vesc_ride_config_t *out);
bool vesc_ride_mode_get_status(vesc_ride_status_t *out);
bool vesc_ride_mode_request_config(void);
bool vesc_ride_mode_set_config(const vesc_ride_config_t *cfg,
                               uint16_t *seq_out);
bool vesc_ride_mode_select(uint8_t profile, uint16_t *seq_out);
void vesc_ride_mode_set_screen_active(bool active);

#ifdef __cplusplus
}
#endif
