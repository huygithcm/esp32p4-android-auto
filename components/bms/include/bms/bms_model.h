#pragma once

/* Normalized BMS snapshot — the boundary between the acquisition backend
 * (BLE transport + protocol driver) and every consumer (BMS tab, logging,
 * the phone app later). Consumers must only ever read through
 * bms_model_get(); nothing outside components/bms publishes.
 *
 * Units are scaled integers on purpose. Floats invite silent scale drift
 * between a driver and the UI, and every field a BMS reports is integral at
 * the wire level anyway.
 *
 * A field that the protocol did not carry must be left *invalid*, never zero:
 * a 0 mV cell and an unreported cell look identical otherwise, and the UI has
 * no way to tell "this pack has no cell 17" from "cell 17 is dead". That is
 * what valid_mask is for.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* JK's 32S frame layout is the widest we decode; keep the array sized to the
 * protocol, not to any particular pack. */
#define BMS_MAX_CELLS  32
/* JK02_32S carries MOS + up to 5 external probes. */
#define BMS_MAX_TEMPS  5

/* Link state, driven by the transport. The UI is expected to render these
 * distinctly — "no data yet" and "data went stale" are different faults and
 * lead the rider to different actions. */
typedef enum {
    BMS_LINK_DISABLED = 0,   /* feature off in Kconfig or settings          */
    BMS_LINK_UNBOUND,        /* enabled, but no peer address bound yet      */
    BMS_LINK_SCANNING,
    BMS_LINK_CONNECTING,
    BMS_LINK_PROBING,        /* connected; device-info handshake in flight  */
    BMS_LINK_LIVE,           /* snapshot fresh                              */
    BMS_LINK_STALE,          /* link up or down, but data older than the
                                freshness window — show last values greyed  */
    BMS_LINK_UNSUPPORTED,    /* peer answered, but no driver matched it     */
} bms_link_state_t;

/* valid_mask bits. Cell and temperature arrays are covered element-wise by
 * cell_valid_mask / temp_valid_mask instead, so a pack with a broken probe
 * still publishes the probes that do work. */
#define BMS_V_PACK_MV        (1u << 0)
#define BMS_V_CURRENT        (1u << 1)
#define BMS_V_SOC            (1u << 2)
#define BMS_V_SOH            (1u << 3)
#define BMS_V_REMAINING      (1u << 4)
#define BMS_V_NOMINAL        (1u << 5)
#define BMS_V_CYCLES         (1u << 6)
#define BMS_V_CELLS          (1u << 7)
#define BMS_V_TEMPS          (1u << 8)
#define BMS_V_MOS_TEMP       (1u << 9)
#define BMS_V_MOSFETS        (1u << 10)
#define BMS_V_BALANCE        (1u << 11)
#define BMS_V_ALARMS         (1u << 12)
/* Wire resistance does not arrive with the cell voltages — on JK it lives in
 * the settings frame — so it carries its own element-wise mask as well. */
#define BMS_V_WIRE_RES        (1u << 13)
#define BMS_V_BALANCE_CURRENT (1u << 14)
#define BMS_V_CYCLE_CAPACITY  (1u << 15)
#define BMS_V_HEATER          (1u << 16)
/* Emergency countdown only — it is 32S-only and 16-bit. */
#define BMS_V_TIMERS          (1u << 17)
/* Smart-sleep countdown. Separate bit because it exists on BOTH layouts while
 * the emergency timer does not, so one mask could not describe both. */
#define BMS_V_SLEEP_TIMER     (1u << 18)
/* Peer name / firmware versions / detected layout. The UI asks for a device
 * name and the backend used to answer with a constant, which hid the one
 * thing worth seeing: whether the 24S or 32S layout was picked. A wrong pick
 * still renders plausible values, so putting it on screen turns a
 * serial-console check into something the rider can read. */
#define BMS_V_IDENTITY        (1u << 19)

typedef struct {
    /* ---- pack level ---------------------------------------------------- */
    int32_t  pack_mv;
    /* Sign convention is fixed HERE, at the model boundary, and every driver
     * must conform: current > 0 means the pack is being DISCHARGED, < 0 means
     * charging or regen. That matches how the VESC telemetry on the dashboard
     * already reads, so the two numbers move the same way on screen. JK's wire
     * format is the opposite and its driver inverts. */
    int32_t  pack_current_ma;
    uint16_t soc_permille;       /* 0..1000 */
    uint16_t soh_permille;       /* 0..1000 */
    int32_t  remaining_mah;
    int32_t  nominal_mah;
    uint32_t cycle_count;

    /* ---- cells ---------------------------------------------------------- */
    uint8_t  cell_count;         /* number of populated entries in cell_mv   */
    uint16_t cell_mv[BMS_MAX_CELLS];
    uint32_t cell_valid_mask;    /* bit i set => cell_mv[i] is meaningful    */
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint16_t cell_delta_mv;
    uint8_t  cell_min_idx;       /* 0-based, unlike JK's own 1-based field   */
    uint8_t  cell_max_idx;

    /* ---- temperatures --------------------------------------------------- */
    uint8_t  temp_count;
    int16_t  temp_deci_c[BMS_MAX_TEMPS];
    uint32_t temp_valid_mask;
    int16_t  mos_temp_deci_c;

    /* ---- switch / balance state ----------------------------------------- */
    bool     chg_mos_on;
    bool     dsg_mos_on;
    bool     balancing;
    uint32_t balance_mask;       /* bit i set => cell i is being balanced;
                                    0 when the driver cannot tell which      */

    /* ---- faults --------------------------------------------------------- */
    uint32_t alarm_raw;          /* protocol-specific, kept for diagnostics  */

    /* ---- balance wiring ------------------------------------------------- */
    /* Per-cell balance-lead resistance. On JK this comes from the settings
     * frame, not the cell frame, so it is carried forward between samples and
     * has its own mask: a genuine 0.000 ohm reading must not look like an
     * absent one. */
    uint16_t wire_res_mohm[BMS_MAX_CELLS];
    uint32_t wire_res_valid_mask;
    int32_t  balance_current_ma;  /* >0 = balancing into the cell            */

    /* ---- extended pack figures ------------------------------------------ */
    int32_t  cycle_capacity_mah;  /* lifetime charge throughput              */
    int32_t  heater_current_ma;
    bool     heater_on;
    uint16_t emergency_timer_s;   /* JK02_32S only, 16-bit on the wire       */
    /* Smart-sleep countdown. 32-bit because the pack can be configured to
     * idle for a day and 86400 does not fit in 16 bits. */
    uint32_t sleep_timer_s;

    /* ---- identity ------------------------------------------------------- */
    char     peer_name[32];      /* advertised BLE name, e.g. "JK-B2A24S15P";
                                    sized to the 31-byte BLE name limit        */
    char     hw_version[12];     /* as reported by the device-info frame     */
    char     sw_version[12];
    uint8_t  cell_layout;        /* 24 or 32; 0 while undetected             */

    /* ---- provenance ----------------------------------------------------- */
    uint32_t valid_mask;
    uint32_t sample_seq;         /* increments once per published frame      */
    int64_t  rx_timestamp_us;    /* esp_timer_get_time() at frame completion */
    uint8_t  driver_id;          /* BMS_DRIVER_* below                       */
    int8_t   rssi_dbm;           /* 0 when unknown                           */
} bms_snapshot_t;

#define BMS_DRIVER_NONE     0
#define BMS_DRIVER_JK_BLE   1

/* Diagnostic counters. Cheap to keep and the only way to tell "the BMS is
 * quiet" apart from "we are dropping every frame on CRC". */
typedef struct {
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t length_errors;
    uint32_t dropped_fragments;  /* notification lost because the queue was full */
    uint32_t reconnects;
    uint32_t timeouts;
} bms_diag_t;

/* A snapshot older than this is reported as STALE. JK cell-info frames arrive
 * roughly once a second under the community's default throttle, so five
 * seconds is three missed frames — the same rule vesc_rt_data uses. */
#define BMS_FRESH_WINDOW_MS  5000

/* Publish (backend only). Copies under a lock so readers never observe a
 * half-written cell array. Stamps sample_seq and rx_timestamp_us itself. */
void bms_model_publish(bms_snapshot_t *snap);

/* Atomic read. Returns false and leaves *out untouched when nothing has ever
 * been published, so a caller cannot mistake a zeroed struct for real data. */
bool bms_model_get(bms_snapshot_t *out);

/* Milliseconds since the last publish, or UINT32_MAX when never published. */
uint32_t bms_model_age_ms(void);

/* Link state is owned by the transport; the model just carries it so the UI
 * has a single place to read from. bms_model_link_state() folds in staleness:
 * a LIVE link whose snapshot aged out is reported as STALE. */
void             bms_model_set_link_state(bms_link_state_t st);
bms_link_state_t bms_model_link_state(void);
const char      *bms_model_link_state_str(bms_link_state_t st);

/* Diagnostics. The backend bumps counters through the typed helpers so the
 * struct stays private to the model and readers never race a partial update. */
typedef enum {
    BMS_DIAG_FRAME_OK = 0,
    BMS_DIAG_CRC_ERROR,
    BMS_DIAG_LENGTH_ERROR,
    BMS_DIAG_DROPPED_FRAGMENT,
    BMS_DIAG_RECONNECT,
    BMS_DIAG_TIMEOUT,
} bms_diag_event_t;

void              bms_model_diag_bump(bms_diag_event_t ev);
const bms_diag_t *bms_model_diag(void);

/* Reset everything back to "never published". Used when the bound peer is
 * cleared so the tab cannot keep showing a previous pack's numbers. */
void bms_model_reset(void);

#ifdef __cplusplus
}
#endif
