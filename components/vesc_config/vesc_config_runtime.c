/*
 * Runtime layer: firmware auto-detect + table selection, in-RAM config values,
 * dirty tracking, the emulator-vs-CAN branch, and the public API the UI calls.
 *
 * The UI talks only to vesc_config.h and is oblivious to whether a real VESC
 * (CAN GET/SET) or the emulator (in-RAM defaults) backs the data.
 */
#include "vesc_config_priv.h"
#include "vesc_config/vesc_config.h"

#include "dev_settings.h"
#include "app_fs.h"
#include "esp_log.h"
#include "vesc_can/buffer.h"
#include "vesc_can/vesc_datatypes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "vesc_cfg";

/* Firmware version used for the emulator's local config. */
#define EMU_FW_MAJOR 6
#define EMU_FW_MINOR 5

typedef struct {
    const vc_table_t *table;
    vc_value_t       *vals;     /* param_count entries */
    uint8_t          *dirty;    /* param_count flags */
    bool              any_dirty;
    bool              read_ok;   /* vals reflect a real read-back, not the seed */
} live_cfg_t;

static live_cfg_t s_cfg[2];          /* [VC_MCCONF], [VC_APPCONF] */
static bool       s_emulator;
static bool       s_ready;
static bool       s_readonly;
static uint8_t    s_fw_major, s_fw_minor;

/* Single in-flight UI request (real mode). */
static bool         s_req_active;
static vc_kind_t    s_req_kind;
static bool         s_req_is_write;
static bool         s_req_defaults;
static vc_done_cb_t s_ui_cb;
static void        *s_ui_user;

/* Detection request (shares the single in-flight slot). */
static vc_detect_cb_t s_detect_ui_cb;
static void          *s_detect_ui_user;

/* Manual measurement request (R/L, linkage, encoder, hall). */
typedef enum { MEAS_NONE = 0, MEAS_RL, MEAS_LINKAGE, MEAS_ENCODER, MEAS_HALL } meas_kind_t;
static meas_kind_t   s_meas_kind;
static vc_meas_cb_t  s_meas_ui_cb;
static vc_hall_cb_t  s_hall_ui_cb;
static void         *s_meas_ui_user;

/* Config backups live on the shared SPIFFS mount owned by app_fs (dev_settings),
 * so they coexist with the trip file without a circular dependency. */
static uint8_t      s_blob_buf[600];   /* serialize/read scratch (single in-flight) */
static uint8_t      s_cmp_buf[600];    /* compare scratch (dedup against newest) */

#define VC_BACKUP_SLOTS 10   /* rolling history per kind */

static const char *backup_key(vc_kind_t kind) { return kind == VC_MCCONF ? "mc" : "app"; }
static bool fs_ready(void) { return app_fs_ready(); }

/* backup file: /vescfs/<mc|app>_<slot>.bin, content = [uint32 seq][config blob] */
static void slot_path(char *buf, size_t n, vc_kind_t kind, int slot)
{
    snprintf(buf, n, "%s/%s_%d.bin", app_fs_base(), backup_key(kind), slot);
}

/* Reads a slot's sequence header; false if the slot file is missing/short. */
static bool slot_seq(vc_kind_t kind, int slot, uint32_t *seq)
{
    char path[48];
    slot_path(path, sizeof path, kind, slot);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint32_t s = 0;
    size_t r = fread(&s, 1, sizeof s, f);
    fclose(f);
    if (r != sizeof s) return false;
    if (seq) *seq = s;
    return true;
}

/* Fills slots[]/seqs[] with valid backups sorted newest-first. Returns count. */
static int backup_sorted(vc_kind_t kind, int *slots, uint32_t *seqs)
{
    int cnt = 0;
    for (int i = 0; i < VC_BACKUP_SLOTS; i++) {
        uint32_t s;
        if (slot_seq(kind, i, &s)) { slots[cnt] = i; seqs[cnt] = s; cnt++; }
    }
    for (int i = 1; i < cnt; i++) {           /* insertion sort, desc by seq */
        int si = slots[i]; uint32_t sq = seqs[i]; int j = i - 1;
        while (j >= 0 && seqs[j] < sq) { slots[j+1]=slots[j]; seqs[j+1]=seqs[j]; j--; }
        slots[j+1] = si; seqs[j+1] = sq;
    }
    return cnt;
}

void vesc_config_fs_ensure(void) { app_fs_ensure(); }
int  vesc_config_fs_state(void) { return app_fs_state(); }

/* ---- table selection -------------------------------------------------- */

static const vc_version_t *find_version(uint8_t major, uint8_t minor)
{
    for (size_t i = 0; i < g_vc_version_count; i++) {
        if (g_vc_versions[i].major == major && g_vc_versions[i].minor == minor) {
            return &g_vc_versions[i];
        }
    }
    return NULL;
}

/* Best fallback when the exact version isn't baked: highest same-major version
 * not exceeding the requested minor; else highest same-major; else first. */
static const vc_version_t *find_fallback(uint8_t major, uint8_t minor)
{
    const vc_version_t *best = NULL;
    for (size_t i = 0; i < g_vc_version_count; i++) {
        const vc_version_t *v = &g_vc_versions[i];
        if (v->major != major) continue;
        if (v->minor <= minor) {
            if (!best || v->minor > best->minor) best = v;
        }
    }
    if (best) return best;
    for (size_t i = 0; i < g_vc_version_count; i++) {
        if (g_vc_versions[i].major == major) return &g_vc_versions[i];
    }
    return g_vc_version_count ? &g_vc_versions[0] : NULL;
}

static void free_live(live_cfg_t *c)
{
    free(c->vals);
    free(c->dirty);
    c->vals = NULL;
    c->dirty = NULL;
    c->table = NULL;
    c->any_dirty = false;
}

static bool alloc_live(live_cfg_t *c, const vc_table_t *t)
{
    free_live(c);
    c->vals = calloc(t->param_count, sizeof(vc_value_t));
    c->dirty = calloc(t->param_count, 1);
    if (!c->vals || !c->dirty) {
        free_live(c);
        return false;
    }
    c->table = t;
    c->read_ok = false;   /* vals are the local seed until a real read populates them */
    vesc_config_load_defaults(t, c->vals);
    return true;
}

static void self_check(const vc_table_t *t)
{
    uint32_t got = vesc_config_signature(t);
    if (got != t->expected_signature) {
        ESP_LOGE(TAG, "%s v%u.%02u SIGNATURE MISMATCH computed 0x%08X baked 0x%08X",
                 t->kind ? "appconf" : "mcconf", t->fw_major, t->fw_minor,
                 got, t->expected_signature);
    } else {
        ESP_LOGI(TAG, "%s v%u.%02u sig OK 0x%08X",
                 t->kind ? "appconf" : "mcconf", t->fw_major, t->fw_minor, got);
    }
}

static bool select_version(const vc_version_t *v, bool readonly)
{
    if (!v || !v->mcconf || !v->appconf) {
        return false;
    }
    if (!alloc_live(&s_cfg[VC_MCCONF], v->mcconf) ||
        !alloc_live(&s_cfg[VC_APPCONF], v->appconf)) {
        ESP_LOGE(TAG, "out of memory allocating config");
        free_live(&s_cfg[VC_MCCONF]);
        free_live(&s_cfg[VC_APPCONF]);
        return false;
    }
    s_fw_major = v->major;
    s_fw_minor = v->minor;
    s_readonly = readonly;
    self_check(v->mcconf);
    self_check(v->appconf);
    s_ready = true;
    ESP_LOGI(TAG, "config tables selected: v%u.%02u%s",
             v->major, v->minor, readonly ? " (READ-ONLY fallback)" : "");
    return true;
}

static void on_fw(uint8_t major, uint8_t minor, bool ok, void *user)
{
    (void)user;
    if (!ok) {
        ESP_LOGW(TAG, "no FW_VERSION reply — config menu unavailable");
        return;  /* s_ready stays false; UI shows "VESC not detected" */
    }
    ESP_LOGI(TAG, "downstream VESC firmware %u.%02u", major, minor);
    const vc_version_t *v = find_version(major, minor);
    if (v) {
        select_version(v, false);
    } else {
        const vc_version_t *fb = find_fallback(major, minor);
        ESP_LOGW(TAG, "FW %u.%02u not baked — falling back read-only", major, minor);
        select_version(fb, true);
    }
}

/* ---- lifecycle -------------------------------------------------------- */

void vesc_config_init(void)
{
    s_emulator = settings_get_vesc_emulator();
    vct_init();
    if (s_emulator) {
        const vc_version_t *v = find_version(EMU_FW_MAJOR, EMU_FW_MINOR);
        if (!v && g_vc_version_count) v = &g_vc_versions[0];
        if (select_version(v, false)) {
            ESP_LOGW(TAG, "emulator config (local, v%u.%02u)", s_fw_major, s_fw_minor);
        }
    }
}

void vesc_config_probe_fw(void)
{
    if (s_emulator) {
        return;  /* already selected in init */
    }
    vct_probe_fw(on_fw, NULL);
}

bool vesc_config_ready(void) { return s_ready; }
bool vesc_config_is_emulator(void) { return s_emulator; }
bool vesc_config_is_readonly(void) { return s_readonly; }
bool vesc_config_read_ok(vc_kind_t kind) { return s_cfg[kind].read_ok; }

void vesc_config_get_fw(uint8_t *major, uint8_t *minor)
{
    if (major) *major = s_fw_major;
    if (minor) *minor = s_fw_minor;
}

const vc_table_t *vesc_config_table(vc_kind_t kind)
{
    return s_cfg[kind].table;
}

/* ---- dirty helpers ---------------------------------------------------- */

static void clear_dirty(vc_kind_t kind)
{
    live_cfg_t *c = &s_cfg[kind];
    if (c->table && c->dirty) {
        memset(c->dirty, 0, c->table->param_count);
    }
    c->any_dirty = false;
}

static void mark_all_dirty(vc_kind_t kind)
{
    live_cfg_t *c = &s_cfg[kind];
    if (c->table && c->dirty) {
        memset(c->dirty, 1, c->table->param_count);
        c->any_dirty = true;
    }
}

/* ---- read / write ----------------------------------------------------- */

/* Persist the just-read config to flash so it can be restored after a wipe.
 * Runs on the CAN task (real-mode read completion) — never the LVGL thread, so
 * the NVS commit can't stall rendering. */
static void backup_save(vc_kind_t kind)
{
    if (!fs_ready() || !s_cfg[kind].table) return;
    size_t n = vesc_config_serialize(s_cfg[kind].table, s_cfg[kind].vals,
                                     s_blob_buf, sizeof s_blob_buf);
    if (n == 0) return;

    int slots[VC_BACKUP_SLOTS];
    uint32_t seqs[VC_BACKUP_SLOTS];
    int cnt = backup_sorted(kind, slots, seqs);

    /* Dedup: if identical to the newest backup, don't burn a slot. */
    if (cnt > 0) {
        char path[48];
        slot_path(path, sizeof path, kind, slots[0]);
        FILE *f = fopen(path, "rb");
        if (f) {
            uint32_t seq;
            fread(&seq, 1, sizeof seq, f);
            size_t m = fread(s_cmp_buf, 1, sizeof s_cmp_buf, f);
            fclose(f);
            if (m == n && memcmp(s_cmp_buf, s_blob_buf, n) == 0) {
                return;  /* unchanged since last backup */
            }
        }
    }

    /* Pick a free slot, else overwrite the oldest. New seq = newest + 1. */
    int target = -1;
    for (int i = 0; i < VC_BACKUP_SLOTS && target < 0; i++) {
        uint32_t s;
        if (!slot_seq(kind, i, &s)) target = i;   /* empty */
    }
    uint32_t newseq = (cnt > 0) ? seqs[0] + 1 : 1;
    if (target < 0) target = slots[cnt - 1];       /* oldest (sorted desc) */

    char path[48];
    slot_path(path, sizeof path, kind, target);
    FILE *f = fopen(path, "wb");
    if (!f) { ESP_LOGW(TAG, "backup open %s failed", path); return; }
    fwrite(&newseq, 1, sizeof newseq, f);
    fwrite(s_blob_buf, 1, n, f);
    fclose(f);
    ESP_LOGI(TAG, "backed up %s #%u (%u B) slot %d",
             kind ? "appconf" : "mcconf", (unsigned)newseq, (unsigned)n, target);
}

/* Runs on the CAN/timer task (real mode). Updates dirty state then forwards to
 * the UI callback, which marshals to the LVGL thread itself. */
static void runtime_done(vc_result_t res, void *user)
{
    (void)user;
    vc_kind_t k = s_req_kind;
    if (res == VC_OK) {
        if (s_req_is_write) {
            clear_dirty(k);
        } else if (s_req_defaults) {
            /* GET_*_DEFAULT: vals now hold the firmware's real defaults; the
             * user explicitly asked to load them, so writing is intentional. */
            s_cfg[k].read_ok = true;
            mark_all_dirty(k);
        } else {
            s_cfg[k].read_ok = true;   /* vals now reflect the real controller config */
            clear_dirty(k);
            if (!s_emulator) {
                backup_save(k);        /* auto-backup the real controller config */
            }
        }
    }
    vc_done_cb_t cb = s_ui_cb;
    void *u = s_ui_user;
    s_req_active = false;
    if (cb) {
        cb(k, res, u);
    }
}

vc_result_t vesc_config_read(vc_kind_t kind, bool defaults, vc_done_cb_t cb, void *user)
{
    if (!s_ready || !s_cfg[kind].table) {
        return VC_ERR_NO_TABLE;
    }
    if (s_emulator) {
        if (defaults) {
            vesc_config_load_defaults(s_cfg[kind].table, s_cfg[kind].vals);
            mark_all_dirty(kind);
        } else {
            clear_dirty(kind);
        }
        s_cfg[kind].read_ok = true;   /* local copy is authoritative in emulator */
        if (cb) cb(kind, VC_OK, user);
        return VC_OK;
    }
    if (s_req_active) {
        return VC_ERR_BUSY;
    }
    s_req_active = true;
    s_req_kind = kind;
    s_req_is_write = false;
    s_req_defaults = defaults;
    s_ui_cb = cb;
    s_ui_user = user;
    vc_result_t r = vct_get(s_cfg[kind].table, defaults, s_cfg[kind].vals,
                            runtime_done, NULL);
    if (r != VC_OK) {
        s_req_active = false;
    }
    return r;
}

vc_result_t vesc_config_write(vc_kind_t kind, vc_done_cb_t cb, void *user)
{
    if (!s_ready || !s_cfg[kind].table) {
        return VC_ERR_NO_TABLE;
    }
    if (s_readonly) {
        return VC_ERR_READONLY;
    }
    /* SAFETY: never push a full config blob that wasn't read back from the
     * controller — that would overwrite every untouched param with a local
     * default and wipe the user's settings. */
    if (!s_cfg[kind].read_ok) {
        return VC_ERR_NO_READ;
    }
    if (s_emulator) {
        clear_dirty(kind);
        if (cb) cb(kind, VC_OK, user);
        return VC_OK;
    }
    if (s_req_active) {
        return VC_ERR_BUSY;
    }
    s_req_active = true;
    s_req_kind = kind;
    s_req_is_write = true;
    s_req_defaults = false;
    s_ui_cb = cb;
    s_ui_user = user;
    vc_result_t r = vct_set(s_cfg[kind].table, s_cfg[kind].vals, runtime_done, NULL);
    if (r != VC_OK) {
        s_req_active = false;
    }
    return r;
}

/* ---- FOC detection ---------------------------------------------------- */

static void detect_done(int result, void *user)
{
    (void)user;
    vc_detect_cb_t cb = s_detect_ui_cb;
    void *u = s_detect_ui_user;
    s_req_active = false;
    ESP_LOGI(TAG, "detection result %d", result);
    if (cb) cb(result, u);
}

vc_result_t vesc_config_detect_foc(bool detect_can, double max_power_loss,
                                   double min_current_in, double max_current_in,
                                   double openloop_rpm, double sl_erpm,
                                   vc_detect_cb_t cb, void *user)
{
    if (!s_ready) {
        return VC_ERR_NO_TABLE;
    }
    if (s_emulator) {
        return VC_ERR_READONLY;   /* no real motor to spin */
    }
    if (s_req_active) {
        return VC_ERR_BUSY;
    }
    s_req_active = true;
    s_detect_ui_cb = cb;
    s_detect_ui_user = user;
    vc_result_t r = vct_detect_foc(detect_can, max_power_loss, min_current_in,
                                   max_current_in, openloop_rpm, sl_erpm,
                                   detect_done, NULL);
    if (r != VC_OK) {
        s_req_active = false;
    }
    return r;
}

/* ---- manual FOC measurements ------------------------------------------ */

/* Parses the raw reply on the CAN/timer task and forwards a typed result. */
static void meas_raw_done(int ok, const uint8_t *p, unsigned int len, void *user)
{
    (void)user;
    meas_kind_t  k   = s_meas_kind;
    vc_meas_cb_t mcb = s_meas_ui_cb;
    vc_hall_cb_t hcb = s_hall_ui_cb;
    void        *u   = s_meas_ui_user;
    s_req_active = false;

    if (!ok) {
        if (k == MEAS_HALL) { if (hcb) hcb(0, NULL, -1, u); }
        else                { if (mcb) mcb(0, 0, 0, 0, u); }
        return;
    }

    int32_t ind = 0;
    switch (k) {
    case MEAS_RL: {
        /* Wire: R = ohms*1e6, L/Ld-Lq = microhenries*1e3. The config params
         * foc_motor_l / foc_motor_ld_lq_diff are in HENRIES, so convert
         * uH -> H (i.e. /1e9 overall), matching VESC Tool's `ind = l * 1e-6`. */
        double r = (len >= 4) ? (double)buffer_get_int32(p, &ind) / 1e6 : 0.0;
        double l = (len >= 8) ? (double)buffer_get_int32(p, &ind) / 1e9 : 0.0;
        double lddiff = (len >= 12) ? (double)buffer_get_int32(p, &ind) / 1e9 : 0.0;
        if (mcb) mcb(1, r, l, lddiff, u);
        break;
    }
    case MEAS_LINKAGE: {
        double flux = (len >= 4) ? (double)buffer_get_int32(p, &ind) / 1e7 : 0.0;
        if (mcb) mcb(1, flux, 0, 0, u);
        break;
    }
    case MEAS_ENCODER: {
        double off   = (len >= 4) ? (double)buffer_get_int32(p, &ind) / 1e6 : 0.0;
        double ratio = (len >= 8) ? (double)buffer_get_int32(p, &ind) / 1e6 : 0.0;
        int    inv   = (len >= 9) ? (int)(int8_t)p[ind] : 0;
        if (mcb) mcb(1, off, ratio, (double)inv, u);
        break;
    }
    case MEAS_HALL: {
        if (len >= 9) {
            uint8_t table[8];
            for (int i = 0; i < 8; i++) table[i] = p[i];
            if (hcb) hcb(1, table, (int)p[8], u);
        } else if (hcb) {
            hcb(0, NULL, -1, u);
        }
        break;
    }
    default:
        break;
    }
}

static vc_result_t start_meas(meas_kind_t k, uint8_t cmd, const uint8_t *body,
                              unsigned int body_len, vc_meas_cb_t mcb,
                              vc_hall_cb_t hcb, void *user)
{
    if (!s_ready) return VC_ERR_NO_TABLE;
    if (s_emulator) return VC_ERR_READONLY;   /* no real motor */
    if (s_req_active) return VC_ERR_BUSY;
    s_req_active = true;
    s_meas_kind = k;
    s_meas_ui_cb = mcb;
    s_hall_ui_cb = hcb;
    s_meas_ui_user = user;
    /* 60 s: R/L is quick but flux-linkage openloop spins the motor for a while. */
    vc_result_t r = vct_request_raw(cmd, body, body_len, 60000, meas_raw_done, NULL);
    if (r != VC_OK) s_req_active = false;
    return r;
}

vc_result_t vesc_config_measure_rl(vc_meas_cb_t cb, void *user)
{
    return start_meas(MEAS_RL, COMM_DETECT_MOTOR_R_L, NULL, 0, cb, NULL, user);
}

vc_result_t vesc_config_measure_linkage(double current, double erpm_per_sec, double low_duty,
                                        double resistance, double inductance,
                                        vc_meas_cb_t cb, void *user)
{
    uint8_t body[20];
    int32_t ind = 0;
    buffer_append_int32(body, (int32_t)llround(current      * 1e3), &ind);
    buffer_append_int32(body, (int32_t)llround(erpm_per_sec * 1e3), &ind);
    buffer_append_int32(body, (int32_t)llround(low_duty     * 1e3), &ind);
    buffer_append_int32(body, (int32_t)llround(resistance   * 1e6), &ind);
    buffer_append_int32(body, (int32_t)llround(inductance   * 1e8), &ind);
    return start_meas(MEAS_LINKAGE, COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP,
                      body, (unsigned)ind, cb, NULL, user);
}

vc_result_t vesc_config_measure_encoder(double current, vc_meas_cb_t cb, void *user)
{
    uint8_t body[4];
    int32_t ind = 0;
    buffer_append_int32(body, (int32_t)llround(current * 1e3), &ind);
    return start_meas(MEAS_ENCODER, COMM_DETECT_ENCODER, body, (unsigned)ind, cb, NULL, user);
}

vc_result_t vesc_config_measure_hall(double current, vc_hall_cb_t cb, void *user)
{
    uint8_t body[4];
    int32_t ind = 0;
    buffer_append_int32(body, (int32_t)llround(current * 1e3), &ind);
    return start_meas(MEAS_HALL, COMM_DETECT_HALL_FOC, body, (unsigned)ind, NULL, cb, user);
}

bool vesc_config_set_double_by_name(vc_kind_t kind, const char *name, double v)
{
    int idx = vesc_config_find_param(kind, name);
    if (idx < 0) return false;
    vesc_config_set_double(kind, idx, v);
    return true;
}

bool vesc_config_set_int_by_name(vc_kind_t kind, const char *name, int32_t v)
{
    int idx = vesc_config_find_param(kind, name);
    if (idx < 0) return false;
    vesc_config_set_int(kind, idx, v);
    return true;
}

/* ---- backup / restore ------------------------------------------------- */

int vesc_config_backup_count(vc_kind_t kind)
{
    if (!fs_ready()) return 0;
    int slots[VC_BACKUP_SLOTS];
    uint32_t seqs[VC_BACKUP_SLOTS];
    return backup_sorted(kind, slots, seqs);
}

bool vesc_config_has_backup(vc_kind_t kind)
{
    return vesc_config_backup_count(kind) > 0;
}

uint32_t vesc_config_backup_seq(vc_kind_t kind, int idx)   /* idx 0 = newest */
{
    if (!fs_ready()) return 0;
    int slots[VC_BACKUP_SLOTS];
    uint32_t seqs[VC_BACKUP_SLOTS];
    int c = backup_sorted(kind, slots, seqs);
    return (idx >= 0 && idx < c) ? seqs[idx] : 0;
}

vc_result_t vesc_config_restore_index(vc_kind_t kind, int idx)
{
    if (!s_ready || !s_cfg[kind].table) return VC_ERR_NO_TABLE;
    if (!fs_ready()) return VC_ERR_INTERNAL;
    int slots[VC_BACKUP_SLOTS];
    uint32_t seqs[VC_BACKUP_SLOTS];
    int c = backup_sorted(kind, slots, seqs);
    if (idx < 0 || idx >= c) return VC_ERR_INTERNAL;

    char path[48];
    slot_path(path, sizeof path, kind, slots[idx]);
    FILE *f = fopen(path, "rb");
    if (!f) return VC_ERR_INTERNAL;
    uint32_t seq;
    fread(&seq, 1, sizeof seq, f);
    size_t n = fread(s_blob_buf, 1, sizeof s_blob_buf, f);
    fclose(f);
    /* deserialize validates the signature → rejects a backup from another FW. */
    if (!vesc_config_deserialize(s_cfg[kind].table, s_blob_buf, n, s_cfg[kind].vals)) {
        return VC_ERR_SIGNATURE;
    }
    s_cfg[kind].read_ok = true;
    mark_all_dirty(kind);   /* user must Write to push the restored config */
    return VC_OK;
}

/* ---- last open page --------------------------------------------------- */

/* ---- value access ----------------------------------------------------- */

int vesc_config_find_param(vc_kind_t kind, const char *name)
{
    const vc_table_t *t = s_cfg[kind].table;
    if (!t) return -1;
    for (uint16_t i = 0; i < t->param_count; i++) {
        if (strcmp(vc_str(t, t->params[i].name_off), name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

double vesc_config_get_double(vc_kind_t kind, int idx)
{
    const live_cfg_t *c = &s_cfg[kind];
    if (!c->table || idx < 0 || idx >= c->table->param_count) return 0.0;
    return c->vals[idx].d;
}

void vesc_config_set_double(vc_kind_t kind, int idx, double v)
{
    live_cfg_t *c = &s_cfg[kind];
    if (!c->table || idx < 0 || idx >= c->table->param_count) return;
    c->vals[idx].d = v;
    c->dirty[idx] = 1;
    c->any_dirty = true;
}

int32_t vesc_config_get_int(vc_kind_t kind, int idx)
{
    const live_cfg_t *c = &s_cfg[kind];
    if (!c->table || idx < 0 || idx >= c->table->param_count) return 0;
    return c->vals[idx].i;
}

void vesc_config_set_int(vc_kind_t kind, int idx, int32_t v)
{
    live_cfg_t *c = &s_cfg[kind];
    if (!c->table || idx < 0 || idx >= c->table->param_count) return;
    c->vals[idx].i = v;
    c->dirty[idx] = 1;
    c->any_dirty = true;
}

/* ---- dirty queries ---------------------------------------------------- */

bool vesc_config_dirty(vc_kind_t kind) { return s_cfg[kind].any_dirty; }

bool vesc_config_param_dirty(vc_kind_t kind, int idx)
{
    const live_cfg_t *c = &s_cfg[kind];
    if (!c->table || !c->dirty || idx < 0 || idx >= c->table->param_count) return false;
    return c->dirty[idx] != 0;
}

void vesc_config_clear_dirty(vc_kind_t kind) { clear_dirty(kind); }

/* ---- navigation ------------------------------------------------------- */

uint16_t vesc_config_group_count(vc_kind_t kind)
{
    const vc_table_t *t = s_cfg[kind].table;
    return t ? t->group_count : 0;
}

const vc_group_t *vesc_config_group(vc_kind_t kind, int gi)
{
    const vc_table_t *t = s_cfg[kind].table;
    if (!t || gi < 0 || gi >= t->group_count) return NULL;
    return &t->groups[gi];
}

const vc_subgroup_t *vesc_config_subgroup(vc_kind_t kind, const vc_group_t *g, int si)
{
    const vc_table_t *t = s_cfg[kind].table;
    if (!t || !g || si < 0 || si >= g->sub_count) return NULL;
    return &t->subgroups[g->first_sub + si];
}

const vc_group_entry_t *vesc_config_entry(vc_kind_t kind, const vc_subgroup_t *sg, int ei)
{
    const vc_table_t *t = s_cfg[kind].table;
    if (!t || !sg || ei < 0 || ei >= sg->entry_count) return NULL;
    return &t->group_entries[sg->first_entry + ei];
}

const vc_param_t *vesc_config_param(vc_kind_t kind, int idx)
{
    const vc_table_t *t = s_cfg[kind].table;
    if (!t || idx < 0 || idx >= t->param_count) return NULL;
    return &t->params[idx];
}

const char *vesc_config_pool(vc_kind_t kind, uint32_t off)
{
    const vc_table_t *t = s_cfg[kind].table;
    return t ? vc_str(t, off) : "";
}
