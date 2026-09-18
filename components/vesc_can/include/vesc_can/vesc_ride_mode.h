/* Public, non-blocking snapshot API for the ride-mode settings screen.
 *
 * VESC Lisp remains the source of truth. The LVGL task only stages edits,
 * queues a whole-config request and observes replies through these snapshots.
 *
 * FORMAT 2 (docs/RIDE_MODE_ABSOLUTE_CURRENT_PLAN.md). A mode now stores an
 * ABSOLUTE motor-current limit in amperes and nothing else:
 *
 *     effective_A = min(requested_A, ESC's l-current-max)
 *
 * The requested value is stored as the rider typed it even when it exceeds
 * what the ESC allows, so raising Motor Current Max later starts using it
 * without re-entering anything. The ESC is never raised to meet a mode -- it
 * stays the master limit, and the firmware's thermal and hardware protections
 * sit underneath regardless.
 *
 * What format 1 had and this does not: per-mode speed limits (speed is Motor
 * Settings' business now, not the ride mode's) and the per-mille current scale
 * (an absolute ampere figure means the same thing on any ESC). Modes are also
 * independent -- 90/40/70 A is a legitimate choice, so there is no ordering
 * rule and ORDER_INVALID is reserved but never returned.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VESC_RIDE_MODE_COUNT                    3u
#define VESC_RIDE_CONFIG_FORMAT_VERSION         2u

/* Entry range, decided with the user: three digits of whole amperes.
 * Carried as deci-amps so a tenth-amp step later costs no format bump, and
 * 9990 leaves plenty of headroom in the uint16 it travels in. There is no
 * ceiling tied to the ESC here on purpose: a value above what the ESC can do
 * is legal to store and simply clamps when applied. */
#define VESC_RIDE_CURRENT_MIN_DA               10u    /*   1.0 A */
#define VESC_RIDE_CURRENT_MAX_DA             9990u    /* 999.0 A */

#define VESC_RIDE_REVERSE_SPEED_MIN_DKMH       10u    /*  1.0 km/h */
#define VESC_RIDE_REVERSE_SPEED_MAX_DKMH       50u    /*  5.0 km/h */
#define VESC_RIDE_REVERSE_CURRENT_MIN_DA       10u    /*  1.0 A */
#define VESC_RIDE_REVERSE_CURRENT_MAX_DA      140u    /* 14.0 A */

typedef enum {
    VESC_RIDE_RESULT_OK = 0,
    VESC_RIDE_RESULT_BAD_LENGTH,
    VESC_RIDE_RESULT_BAD_VERSION,
    VESC_RIDE_RESULT_OUT_OF_RANGE,
    /* Reserved. Format 1 refused a decreasing set of speeds; format 2 has no
     * ordering rule, so this is never returned. The slot stays so the codes
     * after it keep their numbers. */
    VESC_RIDE_RESULT_ORDER_INVALID,
    VESC_RIDE_RESULT_VEHICLE_MOVING,
    VESC_RIDE_RESULT_THROTTLE_NOT_RELEASED,
    VESC_RIDE_RESULT_REVERSE_ACTIVE,
    VESC_RIDE_RESULT_STORAGE_ERROR,
    VESC_RIDE_RESULT_UNSUPPORTED_HARDWARE,
    VESC_RIDE_RESULT_TIMEOUT,
    VESC_RIDE_RESULT_BRAKE_REQUIRED,
    VESC_RIDE_RESULT_INPUT_FAULT,
    VESC_RIDE_RESULT_PARK_REQUIRED,
    VESC_RIDE_RESULT_STALE_REQUEST,
} vesc_ride_result_t;

typedef enum {
    VESC_RIDE_SAFETY_PARK = 0,
    VESC_RIDE_SAFETY_FORWARD,
    VESC_RIDE_SAFETY_REVERSE_READY,
    VESC_RIDE_SAFETY_REVERSE_ACTIVE,
    VESC_RIDE_SAFETY_INTERLOCK,
    VESC_RIDE_SAFETY_FAULT,
} vesc_ride_safety_state_t;

#define VESC_RIDE_SAFETY_VERSION 1u
#define VESC_RIDE_SAFETY_FRESH_MS 1000u
typedef struct {
    bool valid;
    vesc_ride_safety_state_t state;
    uint8_t current_profile;
    vesc_ride_result_t result;
    uint16_t response_seq;
    uint32_t epoch;
    /* Command outcome is retained across subsequent read-only polls. */
    uint16_t command_seq;
    vesc_ride_result_t command_result;
    bool command_pending;
} vesc_ride_safety_t;

typedef struct {
    uint32_t           epoch;
    bool               valid;
    uint16_t           response_seq;
    vesc_ride_result_t last_result;
    uint8_t            format_version;
    uint16_t           config_revision;
    /* What the rider asked for, per mode. Stored verbatim; see the header
     * comment on why it may legitimately exceed the ESC. */
    uint16_t           mode_current_dA[VESC_RIDE_MODE_COUNT];
    /* The ESC's own Motor Current Max, read live by Lisp. Read-only here, and
     * carried on every reply so the screen can show requested against
     * effective without a second round trip. */
    uint16_t           esc_current_max_dA;
    bool               reverse_enabled;
    uint16_t           reverse_speed_dkmh;
    uint16_t           reverse_current_dA;
    bool               persist_pending;
} vesc_ride_config_t;

typedef struct {
    uint32_t epoch;
    bool     valid;
    uint16_t response_seq; /* sequenced status only; legacy packets use zero */
    uint16_t config_revision;
    uint8_t  current_profile;
    /* The active mode's stored figure and what it actually clamps to right
     * now. They differ whenever the mode asks for more than the ESC allows,
     * which is the case worth showing the rider. */
    uint16_t requested_current_dA;
    uint16_t effective_current_dA;
    uint16_t esc_current_max_dA;
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
bool vesc_ride_mode_get_safety(vesc_ride_safety_t *out);
bool vesc_ride_mode_set_park(bool park, uint16_t *seq_out);
bool vesc_ride_mode_request_config(void);
bool vesc_ride_mode_set_config(const vesc_ride_config_t *cfg,
                               uint16_t *seq_out);
bool vesc_ride_mode_select(uint8_t profile, uint16_t *seq_out);
void vesc_ride_mode_set_screen_active(bool active);

#ifdef __cplusplus
}
#endif
