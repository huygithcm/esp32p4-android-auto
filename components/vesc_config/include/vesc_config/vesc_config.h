/*
 * Public runtime API for the on-device VESC controller-config menu.
 *
 * Transport-agnostic: the same read/write/get/set calls work whether the
 * backend is a real VESC over CAN (COMM_GET/SET_MCCONF/APPCONF) or the in-RAM
 * emulator (vesc_sim active). The UI (Super_VESC_Display/custom/vesc_tool_menu.c)
 * talks only to this header and never touches CAN or the table layout.
 */
#pragma once

#include "vesc_config/vesc_config_types.h"
#include "vesc_config/vesc_config_serdes.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VC_MCCONF = 0,
    VC_APPCONF = 1,
} vc_kind_t;

typedef enum {
    VC_OK = 0,
    VC_ERR_TIMEOUT,     /* no response from VESC */
    VC_ERR_SIGNATURE,   /* blob signature didn't match the selected table */
    VC_ERR_NO_TABLE,    /* no config table selected (VESC not detected) */
    VC_ERR_BUSY,        /* another request is already in flight */
    VC_ERR_READONLY,    /* write attempted on a fallback/incompatible table */
    VC_ERR_NO_READ,     /* write attempted before a successful read-back */
    VC_ERR_INTERNAL,
} vc_result_t;

/* Completion callback. In real (CAN) mode it fires from the CAN process task;
 * in emulator mode it fires synchronously inside the read/write call. The UI
 * must marshal any LVGL work to the LVGL thread (lv_async_call). */
typedef void (*vc_done_cb_t)(vc_kind_t kind, vc_result_t result, void *user);

/* ---- lifecycle / detection ---- */

/* Initialise the module. In emulator mode selects a default table immediately;
 * in real mode you must call vesc_config_probe_fw() once the CAN link is up. */
void vesc_config_init(void);

/* Kick a COMM_FW_VERSION probe to the configured target VESC over CAN and pick
 * the matching baked table. No-op in emulator mode. Result is observable via
 * vesc_config_ready()/vesc_config_get_fw() after the reply arrives. */
void vesc_config_probe_fw(void);

/* Select which controller all subsequent read/write/probe/detect ops address.
 * 0 = the global primary head (settings target_vesc_id); non-zero = configure
 * that controller_id instead. Used by the menu's "Head 1 / Head 2" selector on
 * dual-motor boards. */
void vesc_config_set_target(uint8_t controller_id);

bool vesc_config_ready(void);                 /* a table has been selected */
bool vesc_config_is_emulator(void);
bool vesc_config_is_readonly(void);           /* fallback table → writes blocked */

/* True once this kind has been successfully read back from the VESC (or loaded
 * from firmware defaults / emulator). Writing before this is true would push a
 * full blob of locally-seeded defaults and wipe the controller — so writes are
 * refused until then. */
bool vesc_config_read_ok(vc_kind_t kind);
void vesc_config_get_fw(uint8_t *major, uint8_t *minor);
const vc_table_t *vesc_config_table(vc_kind_t kind);

/* ---- read / write ---- */

/* Read config from the VESC (or local defaults in emulator mode). If
 * defaults==true, requests the firmware defaults (GET_*CONF_DEFAULT). The
 * result is delivered via cb. Returns VC_OK if the request was accepted, or an
 * immediate error (VC_ERR_NO_TABLE / VC_ERR_BUSY). */
vc_result_t vesc_config_read(vc_kind_t kind, bool defaults, vc_done_cb_t cb, void *user);

/* Write the current (edited) config back to the VESC. cb reports the ack. */
vc_result_t vesc_config_write(vc_kind_t kind, vc_done_cb_t cb, void *user);

/* ---- FOC motor + sensor detection (COMM_DETECT_APPLY_ALL_FOC) ---- */

/* result >= 0: success (motor count). < 0: firmware error code; the special
 * value -1000 means no reply (timeout). On success the firmware has already
 * applied + stored the detected params, so the caller should re-read mcconf. */
#define VC_DETECT_TIMEOUT (-1000)   /* result value meaning "no reply" */
typedef void (*vc_detect_cb_t)(int result, void *user);

/* Pass 0 for min/max_current_in/openloop_rpm/sl_erpm to let the firmware pick
 * defaults. detect_can = also detect motors daisy-chained on the VESC's CAN.
 * Spins the motor — only meaningful with a real VESC (not the emulator). */
vc_result_t vesc_config_detect_foc(bool detect_can, double max_power_loss,
                                   double min_current_in, double max_current_in,
                                   double openloop_rpm, double sl_erpm,
                                   vc_detect_cb_t cb, void *user);

/* ---- manual FOC detection (VESC Tool "Detect FOC Parameters" flow) ---- */

/* ok=1 success / 0 timeout. measure_rl: a=R(ohm) b=L(H) c=Ld-Lq(H);
 * measure_linkage: a=flux(Wb); measure_encoder: a=offset(deg) b=ratio c=inverted. */
typedef void (*vc_meas_cb_t)(int ok, double a, double b, double c, void *user);
/* res==0 means a valid hall table; table holds 8 entries. */
typedef void (*vc_hall_cb_t)(int ok, const uint8_t *table, int res, void *user);

vc_result_t vesc_config_measure_rl(vc_meas_cb_t cb, void *user);
vc_result_t vesc_config_measure_linkage(double current, double erpm_per_sec, double low_duty,
                                        double resistance, double inductance,
                                        vc_meas_cb_t cb, void *user);
vc_result_t vesc_config_measure_encoder(double current, vc_meas_cb_t cb, void *user);
vc_result_t vesc_config_measure_hall(double current, vc_hall_cb_t cb, void *user);

/* Convenience for the manual-detect "Apply": set a double/int param by name and
 * mark it dirty. Returns false if the name isn't in the current table. */
bool vesc_config_set_double_by_name(vc_kind_t kind, const char *name, double v);
bool vesc_config_set_int_by_name(vc_kind_t kind, const char *name, int32_t v);

/* ---- backup storage (SPIFFS on the 'storage' partition) ---- */

/* Mount/format the backup filesystem (lazily, on a background task — formatting
 * the partition the first time takes a while). Safe to call repeatedly. */
void vesc_config_fs_ensure(void);

typedef enum {
    VC_FS_NONE = 0,
    VC_FS_MOUNTING,
    VC_FS_FORMATTING,   /* first-time format in progress — show on screen */
    VC_FS_READY,
    VC_FS_FAIL,
} vc_fs_state_t;
int vesc_config_fs_state(void);   /* returns vc_fs_state_t */

/* ---- backup to ESP flash ---- */

/* A successful read is auto-saved to flash so the config can be restored if the
 * controller is ever wiped. */
bool        vesc_config_has_backup(vc_kind_t kind);
/* Up to 10 rolling backups are kept per kind. count = how many exist;
 * seq(idx) = the monotonic id of the idx-th newest (idx 0 = newest). */
int         vesc_config_backup_count(vc_kind_t kind);
uint32_t    vesc_config_backup_seq(vc_kind_t kind, int idx);
/* Load the idx-th newest backup into the live config (marks all params dirty so
 * a subsequent Write pushes it to the VESC). VC_ERR_SIGNATURE if that backup was
 * taken on a different firmware version. */
vc_result_t vesc_config_restore_index(vc_kind_t kind, int idx);

/* ---- value access (by params[] index; use find_param to resolve a name) ---- */

int      vesc_config_find_param(vc_kind_t kind, const char *name);  /* idx or -1 */

double   vesc_config_get_double(vc_kind_t kind, int idx);
void     vesc_config_set_double(vc_kind_t kind, int idx, double v);   /* marks dirty */
int32_t  vesc_config_get_int(vc_kind_t kind, int idx);
void     vesc_config_set_int(vc_kind_t kind, int idx, int32_t v);     /* marks dirty */

/* ---- dirty tracking ---- */

bool vesc_config_dirty(vc_kind_t kind);            /* any param changed */
bool vesc_config_param_dirty(vc_kind_t kind, int idx);
void vesc_config_clear_dirty(vc_kind_t kind);

/* ---- navigation helpers (thin wrappers over the selected table) ---- */

uint16_t          vesc_config_group_count(vc_kind_t kind);
const vc_group_t *vesc_config_group(vc_kind_t kind, int gi);
const vc_subgroup_t *vesc_config_subgroup(vc_kind_t kind, const vc_group_t *g, int si);
const vc_group_entry_t *vesc_config_entry(vc_kind_t kind, const vc_subgroup_t *sg, int ei);
const vc_param_t *vesc_config_param(vc_kind_t kind, int idx);
const char       *vesc_config_pool(vc_kind_t kind, uint32_t off);

#ifdef __cplusplus
}
#endif
