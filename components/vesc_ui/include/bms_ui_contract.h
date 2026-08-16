#pragma once

/* Display-only snapshot boundary for the on-device BMS view.
 *
 * The BLE backend owns connections, protocol parsing and the canonical BMS
 * model. The LVGL frontend only requests a non-blocking copy of the latest
 * normalized snapshot. A frontend adapter maps the canonical backend model to
 * this structure; backend code does not depend on LVGL or this display ABI.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMS_UI_ABI_VERSION 3u
#define BMS_UI_MAX_CELLS   32u
#define BMS_UI_MAX_TEMPS   8u

typedef enum {
    BMS_UI_LINK_DISABLED = 0,
    BMS_UI_LINK_UNBOUND,
    BMS_UI_LINK_SCANNING,
    BMS_UI_LINK_WAIT_SLOT,
    BMS_UI_LINK_CONNECTING,
    BMS_UI_LINK_DISCOVERING,
    BMS_UI_LINK_AUTHENTICATING,
    BMS_UI_LINK_LIVE,
    BMS_UI_LINK_STALE,
    BMS_UI_LINK_BACKOFF,
    BMS_UI_LINK_UNSUPPORTED,
    BMS_UI_LINK_ERROR,
} bms_ui_link_state_t;

enum {
    BMS_UI_VALID_PACK_VOLTAGE = 1u << 0,
    BMS_UI_VALID_PACK_CURRENT = 1u << 1,
    BMS_UI_VALID_POWER        = 1u << 2,
    BMS_UI_VALID_SOC          = 1u << 3,
    BMS_UI_VALID_SOH          = 1u << 4,
    BMS_UI_VALID_CAPACITY     = 1u << 5,
    BMS_UI_VALID_CYCLES       = 1u << 6,
    BMS_UI_VALID_CELLS        = 1u << 7,
    BMS_UI_VALID_WIRE_RES     = 1u << 8,
    BMS_UI_VALID_TEMPS        = 1u << 9,
    BMS_UI_VALID_MOS_TEMP     = 1u << 10,
    BMS_UI_VALID_RSSI         = 1u << 11,
    BMS_UI_VALID_SWITCHES     = 1u << 12,
    BMS_UI_VALID_ALARMS       = 1u << 13,
    BMS_UI_VALID_BAL_CURRENT  = 1u << 14,
    BMS_UI_VALID_CYCLE_CAP    = 1u << 15,
    BMS_UI_VALID_HEATER       = 1u << 16,
    BMS_UI_VALID_EMERG_TIMER  = 1u << 17,
    BMS_UI_VALID_SLEEP_TIMER  = 1u << 18,
};

enum {
    BMS_UI_FLAG_CHARGE_MOS_ON    = 1u << 0,
    BMS_UI_FLAG_DISCHARGE_MOS_ON = 1u << 1,
    BMS_UI_FLAG_BALANCING        = 1u << 2,
    BMS_UI_FLAG_HEATER_ON        = 1u << 3,
};

typedef struct {
    uint32_t abi_version;       /* Must be BMS_UI_ABI_VERSION. */
    uint32_t sample_seq;        /* Increments only for a valid parsed sample. */
    uint32_t valid_mask;        /* BMS_UI_VALID_*; missing is not zero. */
    bms_ui_link_state_t link_state;
    uint32_t age_ms;            /* UINT32_MAX means no sample received yet. */

    char device_name[24];       /* Advertisement/user-facing name. */
    char model_name[24];        /* BMS-reported model if available. */
    char driver_name[16];       /* e.g. JK, JBD, Daly. */
    int8_t rssi_dbm;

    int32_t pack_mv;
    int32_t pack_current_ma;    /* Positive = discharge, negative = charge. */
    int32_t power_mw;           /* Same sign convention as current. */
    uint16_t soc_permille;      /* 0..1000. */
    uint16_t soh_permille;      /* 0..1000. */
    uint32_t remaining_mah;
    uint32_t nominal_mah;
    uint32_t cycle_count;

    uint8_t cell_count;
    uint16_t cell_mv[BMS_UI_MAX_CELLS];
    uint32_t cell_valid_mask;   /* bit i set when cell_mv[i] is meaningful. */
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint16_t cell_delta_mv;
    uint8_t cell_min_index;     /* Zero-based. */
    uint8_t cell_max_index;     /* Zero-based. */
    uint32_t balancing_mask;    /* bit0 = cell 1. */
    uint16_t wire_res_mohm[BMS_UI_MAX_CELLS];
    uint32_t wire_res_valid_mask; /* bit i set when wire_res_mohm[i] is valid. */
    int32_t balance_current_ma;
    uint32_t cycle_capacity_mah;
    int32_t heater_current_ma;
    uint32_t emergency_timer_s;
    uint32_t sleep_timer_s;

    uint8_t temp_count;
    int16_t temp_deci_c[BMS_UI_MAX_TEMPS];
    uint32_t temp_valid_mask;
    int16_t mos_temp_deci_c;
    uint32_t status_flags;      /* BMS_UI_FLAG_*. */
    uint32_t alarm_flags;       /* Backend-normalized alarms. */
    uint32_t raw_alarm_code;    /* Vendor value for diagnostics. */
} bms_ui_snapshot_t;

/* Frontend adapter entry points. Both functions must return quickly.
 * get_snapshot copies one internally consistent sample into out and returns
 * true. It returns false when no model provider is installed. set_active is a
 * polling-rate hint: true only while the BMS tab is visible. */
bool bms_ui_backend_get_snapshot(bms_ui_snapshot_t *out);
void bms_ui_backend_set_active(bool active);

#ifdef __cplusplus
}
#endif
