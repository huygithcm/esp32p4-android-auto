/* Weak fallbacks for the ride-mode FE.
 *
 * Real-device builds fail closed until the VESC CAN/Lisp backend supplies
 * strong definitions for this API. The desktop simulator keeps an in-memory
 * fixture so the complete screen can be exercised without an ESC.
 */
#include "vesc_can/vesc_ride_mode.h"

#ifndef LV_REALDEVICE
static vesc_ride_config_t s_cfg = {
    .epoch = 1,
    .valid = true,
    .last_result = VESC_RIDE_RESULT_OK,
    .format_version = VESC_RIDE_CONFIG_FORMAT_VERSION,
    .config_revision = 1,
    .speed_dkmh = {50, 100, 200},
    .current_permille = {300, 600, 1000},
    .reverse_enabled = false,
    .reverse_speed_dkmh = 30,
    .reverse_current_dA = 70,
};
static vesc_ride_status_t s_status = {
    .epoch = 1,
    .valid = true,
    .config_revision = 1,
    .current_profile = 0,
    .active_speed_dkmh = 50,
    .direction_state = 1,
};
static uint16_t s_seq;
#endif

__attribute__((weak)) bool vesc_ride_mode_get_config(vesc_ride_config_t *out)
{
#ifdef LV_REALDEVICE
    (void)out;
    return false;
#else
    if (!out) return false;
    *out = s_cfg;
    return true;
#endif
}

__attribute__((weak)) bool vesc_ride_mode_get_status(vesc_ride_status_t *out)
{
#ifdef LV_REALDEVICE
    (void)out;
    return false;
#else
    if (!out) return false;
    *out = s_status;
    return true;
#endif
}

__attribute__((weak)) bool vesc_ride_mode_request_config(void)
{
#ifdef LV_REALDEVICE
    return false;
#else
    return true;
#endif
}

__attribute__((weak)) bool vesc_ride_mode_set_config(
    const vesc_ride_config_t *cfg, uint16_t *seq_out)
{
#ifdef LV_REALDEVICE
    (void)cfg;
    (void)seq_out;
    return false;
#else
    if (!cfg) return false;
    s_cfg = *cfg;
    s_cfg.valid = true;
    s_cfg.format_version = VESC_RIDE_CONFIG_FORMAT_VERSION;
    s_cfg.config_revision++;
    s_cfg.response_seq = ++s_seq;
    s_cfg.last_result = VESC_RIDE_RESULT_OK;
    s_cfg.epoch++;
    s_status.config_revision = s_cfg.config_revision;
    s_status.active_speed_dkmh = s_cfg.speed_dkmh[s_status.current_profile];
    s_status.epoch++;
    if (seq_out) *seq_out = s_seq;
    return true;
#endif
}

__attribute__((weak)) bool vesc_ride_mode_select(uint8_t profile,
                                                 uint16_t *seq_out)
{
#ifdef LV_REALDEVICE
    (void)profile;
    (void)seq_out;
    return false;
#else
    if (profile >= VESC_RIDE_MODE_COUNT) return false;
    s_status.current_profile = profile;
    s_status.active_speed_dkmh = s_cfg.speed_dkmh[profile];
    s_status.epoch++;
    if (seq_out) *seq_out = ++s_seq;
    return true;
#endif
}

__attribute__((weak)) void vesc_ride_mode_set_screen_active(bool active)
{
    (void)active;
}
