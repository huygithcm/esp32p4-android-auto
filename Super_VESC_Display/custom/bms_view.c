/* Read-only BMS dashboard frontend.
 *
 * BLE/GATT and vendor parsing live behind the backend model/client APIs. This
 * file maps normalized data to the display and never calls NimBLE directly.
 */
#include "bms_view.h"

#include <stdio.h>
#include <string.h>

#include "bms_ui_contract.h"
#include "gui_guider.h"

#ifdef LV_REALDEVICE
#include "bms/bms_model.h"
#include "ble_bms_client.h"
#include "freertos/FreeRTOS.h"
#endif

#define COL_BG       0x07090A
#define COL_PANEL    0x12181C
#define COL_CARD     0x192329
#define COL_BTN      0x2A3440
#define COL_ACCENT   0xB6FF2E
#define COL_CYAN     0x00A9FF
#define COL_TEXT     0xFFFFFF
#define COL_DIM      0x8A9499
#define COL_WARN     0xFFB020
#define COL_DANGER   0xFF4055

typedef struct {
    lv_obj_t *root;
    lv_obj_t *scroll;
    lv_timer_t *timer;
    bool active;
    bool show_wire;

    lv_obj_t *device;
    lv_obj_t *link;
    lv_obj_t *diagnostic;
    lv_obj_t *pair;
    lv_obj_t *forget;
    lv_obj_t *scan_modal;
    lv_obj_t *scan_list;
    lv_obj_t *soc_arc;
    lv_obj_t *soc;
    lv_obj_t *pack;
    lv_obj_t *current;
    lv_obj_t *power;
    lv_obj_t *cell_summary;
    lv_obj_t *thermal;
    lv_obj_t *health;
    lv_obj_t *switches;
    lv_obj_t *alarm;
    lv_obj_t *detail_pack;
    lv_obj_t *detail_temp;
    lv_obj_t *detail_state;
    lv_obj_t *unsupported;
    lv_obj_t *mode;
    lv_obj_t *empty;
    lv_obj_t *cell_card[BMS_UI_MAX_CELLS];
    lv_obj_t *cell_num[BMS_UI_MAX_CELLS];
    lv_obj_t *cell_value[BMS_UI_MAX_CELLS];
} bms_view_state_t;

static bms_view_state_t s;

#ifdef LV_REALDEVICE
#define BMS_UI_MAX_SCAN_HITS 12
typedef struct {
    ble_addr_t addr;
    char name[28];
    int8_t rssi;
} bms_ui_scan_hit_t;

static portMUX_TYPE s_scan_mux = portMUX_INITIALIZER_UNLOCKED;
static bms_ui_scan_hit_t s_scan_hits[BMS_UI_MAX_SCAN_HITS];
static volatile uint8_t s_scan_hit_count;
static volatile bool s_scan_rebuild_pending;
#endif

static lv_obj_t *label_at(lv_obj_t *parent, const char *text, int x, int y,
                          const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *panel_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

#ifdef LV_REALDEVICE
static void scan_close(void)
{
    ble_bms_set_scan_cb(NULL);
    ble_bms_scan_stop();
    if (s.scan_modal) {
        lv_obj_del_async(s.scan_modal);
        s.scan_modal = NULL;
        s.scan_list = NULL;
    }
}

static void scan_close_cb(lv_event_t *event)
{
    (void)event;
    scan_close();
}

static void scan_select_cb(lv_event_t *event)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    ble_addr_t address;
    bool found = false;
    portENTER_CRITICAL(&s_scan_mux);
    if (index < s_scan_hit_count) {
        address = s_scan_hits[index].addr;
        found = true;
    }
    portEXIT_CRITICAL(&s_scan_mux);
    if (found) ble_bms_bind(&address);
    scan_close();
}

static void scan_rebuild_async(void *arg)
{
    (void)arg;
    bms_ui_scan_hit_t hits[BMS_UI_MAX_SCAN_HITS];
    uint8_t count;
    portENTER_CRITICAL(&s_scan_mux);
    count = s_scan_hit_count;
    memcpy(hits, s_scan_hits, count * sizeof hits[0]);
    s_scan_rebuild_pending = false;
    portEXIT_CRITICAL(&s_scan_mux);

    if (!s.scan_list) return;
    lv_obj_clean(s.scan_list);
    if (count == 0) {
        lv_obj_t *label = lv_label_create(s.scan_list);
        lv_label_set_text(label, "Scanning for JK BMS devices...");
        lv_obj_set_style_text_color(label, lv_color_hex(COL_DIM), 0);
        return;
    }

    for (uint8_t i = 0; i < count; ++i) {
        lv_obj_t *row = lv_btn_create(s.scan_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 42);
        lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 5, 0);
        lv_obj_add_event_cb(row, scan_select_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        char text[96];
        const uint8_t *a = hits[i].addr.val;
        snprintf(text, sizeof text,
                 "%s   %02X:%02X:%02X:%02X:%02X:%02X   %d dBm",
                 hits[i].name[0] ? hits[i].name : "JK BMS",
                 a[5], a[4], a[3], a[2], a[1], a[0], hits[i].rssi);
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserratMedium_12, 0);
        lv_obj_center(label);
    }
}

static void scan_result_cb(const ble_addr_t *addr, const char *name, int8_t rssi)
{
    if (!addr) return;
    bool schedule = false;
    portENTER_CRITICAL(&s_scan_mux);
    uint8_t index = s_scan_hit_count;
    for (uint8_t i = 0; i < s_scan_hit_count; ++i) {
        if (s_scan_hits[i].addr.type == addr->type &&
            memcmp(s_scan_hits[i].addr.val, addr->val, sizeof addr->val) == 0) {
            index = i;
            break;
        }
    }
    if (index < BMS_UI_MAX_SCAN_HITS) {
        if (index == s_scan_hit_count) s_scan_hit_count++;
        s_scan_hits[index].addr = *addr;
        s_scan_hits[index].rssi = rssi;
        snprintf(s_scan_hits[index].name, sizeof s_scan_hits[index].name,
                 "%s", name ? name : "");
        if (!s_scan_rebuild_pending) {
            s_scan_rebuild_pending = true;
            schedule = true;
        }
    }
    portEXIT_CRITICAL(&s_scan_mux);
    if (schedule) lv_async_call(scan_rebuild_async, NULL);
}

static void pair_cb(lv_event_t *event)
{
    (void)event;
    if (!s.root || s.scan_modal) return;

    s.scan_modal = panel_create(s.root, 72, 22, 616, 310);
    lv_obj_move_foreground(s.scan_modal);
    label_at(s.scan_modal, "Pair BMS", 16, 12,
             &lv_font_montserratMedium_20, COL_TEXT);
    lv_obj_t *close = lv_btn_create(s.scan_modal);
    lv_obj_set_pos(close, 552, 8);
    lv_obj_set_size(close, 48, 34);
    lv_obj_set_style_bg_color(close, lv_color_hex(COL_BTN), 0);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "X");
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close, scan_close_cb, LV_EVENT_CLICKED, NULL);

    s.scan_list = lv_obj_create(s.scan_modal);
    lv_obj_set_pos(s.scan_list, 12, 52);
    lv_obj_set_size(s.scan_list, 592, 246);
    lv_obj_set_style_bg_color(s.scan_list, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_border_width(s.scan_list, 0, 0);
    lv_obj_set_style_pad_all(s.scan_list, 8, 0);
    lv_obj_set_flex_flow(s.scan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s.scan_list, LV_DIR_VER);

    portENTER_CRITICAL(&s_scan_mux);
    s_scan_hit_count = 0;
    s_scan_rebuild_pending = false;
    portEXIT_CRITICAL(&s_scan_mux);
    scan_rebuild_async(NULL);
    ble_bms_set_scan_cb(scan_result_cb);
    ble_bms_scan_start();
}

static void forget_cb(lv_event_t *event)
{
    (void)event;
    ble_bms_bind(NULL);
}
#endif

static const char *link_text(bms_ui_link_state_t state)
{
    switch (state) {
    case BMS_UI_LINK_DISABLED:       return "Disabled";
    case BMS_UI_LINK_UNBOUND:        return "Not paired";
    case BMS_UI_LINK_SCANNING:       return "Scanning";
    case BMS_UI_LINK_WAIT_SLOT:      return "Waiting for BLE slot";
    case BMS_UI_LINK_CONNECTING:     return "Connecting";
    case BMS_UI_LINK_DISCOVERING:    return "Discovering";
    case BMS_UI_LINK_AUTHENTICATING: return "Authenticating";
    case BMS_UI_LINK_LIVE:           return "Live";
    case BMS_UI_LINK_STALE:          return "Stale";
    case BMS_UI_LINK_BACKOFF:        return "Retrying";
    case BMS_UI_LINK_UNSUPPORTED:    return "Unsupported";
    case BMS_UI_LINK_ERROR:          return "Connection error";
    default:                         return "Unknown";
    }
}

static uint32_t link_color(bms_ui_link_state_t state)
{
    switch (state) {
    case BMS_UI_LINK_LIVE:        return COL_ACCENT;
    case BMS_UI_LINK_SCANNING:
    case BMS_UI_LINK_CONNECTING:
    case BMS_UI_LINK_DISCOVERING:
    case BMS_UI_LINK_AUTHENTICATING:
    case BMS_UI_LINK_WAIT_SLOT:   return COL_CYAN;
    case BMS_UI_LINK_STALE:
    case BMS_UI_LINK_BACKOFF:     return COL_WARN;
    case BMS_UI_LINK_ERROR:
    case BMS_UI_LINK_UNSUPPORTED: return COL_DANGER;
    default:                      return COL_DIM;
    }
}

static void format_age(char *out, size_t cap, uint32_t age_ms)
{
    if (age_ms == UINT32_MAX) snprintf(out, cap, "No sample");
    else if (age_ms < 1000) snprintf(out, cap, "%lu ms", (unsigned long)age_ms);
    else snprintf(out, cap, "%.1f s", age_ms / 1000.0);
}

static void mode_changed_cb(lv_event_t *event)
{
    lv_obj_t *matrix = lv_event_get_target(event);
    uint16_t selected = lv_btnmatrix_get_selected_btn(matrix);
    if (selected == LV_BTNMATRIX_BTN_NONE) return;
    s.show_wire = selected == 1;
}

static void set_cell_card(uint8_t index, const bms_ui_snapshot_t *d,
                          bool cells_valid, bool wire_valid)
{
    lv_obj_t *card = s.cell_card[index];
    if (!card) return;
    if (index >= d->cell_count || index >= BMS_UI_MAX_CELLS ||
        (s.show_wire && !wire_valid)) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);

    char number[4];
    snprintf(number, sizeof number, "%02u", (unsigned)(index + 1));
    lv_label_set_text(s.cell_num[index], number);

    char value[16];
    bool valid = s.show_wire ? (wire_valid && (d->wire_res_valid_mask & (1u << index)))
                             : (cells_valid && (d->cell_valid_mask & (1u << index)));
    if (!valid) {
        snprintf(value, sizeof value, "--");
    } else if (s.show_wire) {
        snprintf(value, sizeof value, "%.3f R", d->wire_res_mohm[index] / 1000.0);
    } else {
        snprintf(value, sizeof value, "%.3f V", d->cell_mv[index] / 1000.0);
    }
    lv_label_set_text(s.cell_value[index], value);

    uint32_t color = COL_TEXT;
    if (valid && !s.show_wire) {
        if (index == d->cell_min_index) color = COL_CYAN;
        if (index == d->cell_max_index) color = COL_DANGER;
        if (d->balancing_mask & (1u << index)) color = COL_WARN;
    }
    lv_obj_set_style_text_color(s.cell_value[index], lv_color_hex(color), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(card, valid ? 1 : 0, 0);
}

static void render_snapshot(const bms_ui_snapshot_t *d, bool available)
{
    uint32_t valid = available ? d->valid_mask : 0;
    bms_ui_link_state_t state = available ? d->link_state : BMS_UI_LINK_ERROR;
    uint32_t state_color = link_color(state);

    char text[256];
    const char *name = available && d->device_name[0] ? d->device_name : "BMS";
    const char *model = available && d->model_name[0] ? d->model_name : "";
    if (model[0]) snprintf(text, sizeof text, "%s  |  %s", name, model);
    else snprintf(text, sizeof text, "%s", name);
    lv_label_set_text(s.device, text);

    lv_label_set_text(s.link, available ? link_text(state) : "Backend unavailable");
    lv_obj_set_style_text_color(s.link, lv_color_hex(state_color), 0);

    char age[24];
    format_age(age, sizeof age, available ? d->age_ms : UINT32_MAX);
    if (valid & BMS_UI_VALID_RSSI) {
        snprintf(text, sizeof text, "%s  |  %s  |  %d dBm",
                 d->driver_name[0] ? d->driver_name : "BLE", age, d->rssi_dbm);
    } else {
        snprintf(text, sizeof text, "%s  |  %s",
                 available && d->driver_name[0] ? d->driver_name : "BLE", age);
    }
    lv_label_set_text(s.diagnostic, text);

    if (valid & BMS_UI_VALID_SOC) {
        unsigned whole = d->soc_permille / 10u;
        unsigned decimal = d->soc_permille % 10u;
        snprintf(text, sizeof text, "%u.%u%%", whole, decimal);
        lv_label_set_text(s.soc, text);
        lv_arc_set_value(s.soc_arc, d->soc_permille > 1000 ? 1000 : d->soc_permille);
    } else {
        lv_label_set_text(s.soc, "--%");
        lv_arc_set_value(s.soc_arc, 0);
    }
    lv_obj_set_style_arc_color(s.soc_arc, lv_color_hex(state_color), LV_PART_INDICATOR);

    if (valid & BMS_UI_VALID_PACK_VOLTAGE)
        snprintf(text, sizeof text, "Pack   %.2f V", d->pack_mv / 1000.0);
    else snprintf(text, sizeof text, "Pack   --");
    lv_label_set_text(s.pack, text);

    if (valid & BMS_UI_VALID_PACK_CURRENT)
        snprintf(text, sizeof text, "Current   %+.2f A", d->pack_current_ma / 1000.0);
    else snprintf(text, sizeof text, "Current   --");
    lv_label_set_text(s.current, text);

    if (valid & BMS_UI_VALID_POWER)
        snprintf(text, sizeof text, "Power   %+.0f W", d->power_mw / 1000.0);
    else snprintf(text, sizeof text, "Power   --");
    lv_label_set_text(s.power, text);

    if (valid & BMS_UI_VALID_CELLS) {
        snprintf(text, sizeof text, "Cells  %u   Min %.3f   Max %.3f   Delta %u mV",
                 (unsigned)d->cell_count, d->cell_min_mv / 1000.0,
                 d->cell_max_mv / 1000.0, (unsigned)d->cell_delta_mv);
    } else snprintf(text, sizeof text, "Cells  --");
    lv_label_set_text(s.cell_summary, text);

    int first_temp = -1;
    if (valid & BMS_UI_VALID_TEMPS) {
        for (uint8_t i = 0; i < d->temp_count && i < BMS_UI_MAX_TEMPS; ++i) {
            if (d->temp_valid_mask & (1u << i)) { first_temp = i; break; }
        }
    }
    if (first_temp >= 0 && (valid & BMS_UI_VALID_MOS_TEMP))
        snprintf(text, sizeof text, "Battery %.1f C   MOS %.1f C",
                 d->temp_deci_c[first_temp] / 10.0, d->mos_temp_deci_c / 10.0);
    else if (first_temp >= 0)
        snprintf(text, sizeof text, "Battery %.1f C   MOS --",
                 d->temp_deci_c[first_temp] / 10.0);
    else if (valid & BMS_UI_VALID_MOS_TEMP)
        snprintf(text, sizeof text, "Battery --   MOS %.1f C",
                 d->mos_temp_deci_c / 10.0);
    else snprintf(text, sizeof text, "Battery --   MOS --");
    lv_label_set_text(s.thermal, text);

    if ((valid & BMS_UI_VALID_SOH) && (valid & BMS_UI_VALID_CYCLES))
        snprintf(text, sizeof text, "SOH %.1f%%   Cycles %lu",
                 d->soh_permille / 10.0, (unsigned long)d->cycle_count);
    else if (valid & BMS_UI_VALID_CYCLES)
        snprintf(text, sizeof text, "SOH --   Cycles %lu", (unsigned long)d->cycle_count);
    else if (valid & BMS_UI_VALID_SOH)
        snprintf(text, sizeof text, "SOH %.1f%%   Cycles --", d->soh_permille / 10.0);
    else snprintf(text, sizeof text, "SOH --   Cycles --");
    lv_label_set_text(s.health, text);

    if (valid & BMS_UI_VALID_SWITCHES) {
        snprintf(text, sizeof text, "CHG %s | DSG %s | BAL %s",
                 d->status_flags & BMS_UI_FLAG_CHARGE_MOS_ON ? "ON" : "OFF",
                 d->status_flags & BMS_UI_FLAG_DISCHARGE_MOS_ON ? "ON" : "OFF",
                 d->status_flags & BMS_UI_FLAG_BALANCING ? "ON" : "OFF");
    } else snprintf(text, sizeof text, "CHG -- | DSG -- | BAL --");
    lv_label_set_text(s.switches, text);

    uint32_t cell_sum_mv = 0;
    uint8_t valid_cells = 0;
    if (valid & BMS_UI_VALID_CELLS) {
        for (uint8_t i = 0; i < d->cell_count && i < BMS_UI_MAX_CELLS; ++i) {
            if (!(d->cell_valid_mask & (1u << i))) continue;
            cell_sum_mv += d->cell_mv[i];
            valid_cells++;
        }
    }

    char line1[48], line2[48], line3[48], line4[48], line5[48];
    if (valid_cells)
        snprintf(line1, sizeof line1, "Cell average     %.3f V",
                 cell_sum_mv / (1000.0 * valid_cells));
    else snprintf(line1, sizeof line1, "Cell average     --");
    if ((valid & BMS_UI_VALID_CAPACITY) && d->nominal_mah)
        snprintf(line2, sizeof line2, "Capacity         %.2f Ah", d->nominal_mah / 1000.0);
    else snprintf(line2, sizeof line2, "Capacity         --");
    if (valid & BMS_UI_VALID_CAPACITY)
        snprintf(line3, sizeof line3, "Remaining        %.2f Ah", d->remaining_mah / 1000.0);
    else snprintf(line3, sizeof line3, "Remaining        --");
    if ((valid & BMS_UI_VALID_CAPACITY) && d->nominal_mah >= d->remaining_mah)
        snprintf(line4, sizeof line4, "Used capacity    %.2f Ah",
                 (d->nominal_mah - d->remaining_mah) / 1000.0);
    else snprintf(line4, sizeof line4, "Used capacity    --");
    if (valid & BMS_UI_VALID_CYCLE_CAP)
        snprintf(line5, sizeof line5, "Cycle capacity   %.2f Ah",
                 d->cycle_capacity_mah / 1000.0);
    else snprintf(line5, sizeof line5, "Cycle capacity   --");
    snprintf(text, sizeof text, "%s\n%s\n%s\n%s\n%s",
             line1, line2, line3, line4, line5);
    lv_label_set_text(s.detail_pack, text);

    char thermal[192];
    size_t thermal_len = 0;
    if (valid & BMS_UI_VALID_TEMPS) {
        for (uint8_t i = 0; i < d->temp_count && i < BMS_UI_MAX_TEMPS; ++i) {
            if (!(d->temp_valid_mask & (1u << i))) continue;
            int n = snprintf(thermal + thermal_len, sizeof thermal - thermal_len,
                             "T%u  %.1f C%s", (unsigned)(i + 1),
                             d->temp_deci_c[i] / 10.0,
                             (i & 1u) ? "\n" : "     ");
            if (n < 0 || (size_t)n >= sizeof thermal - thermal_len) break;
            thermal_len += (size_t)n;
        }
    }
    if (thermal_len == 0)
        thermal_len = (size_t)snprintf(thermal, sizeof thermal, "T1  --     T2  --\n");
    if (thermal_len && thermal[thermal_len - 1] != '\n' && thermal_len < sizeof thermal - 1)
        thermal[thermal_len++] = '\n';
    thermal[thermal_len] = '\0';
    char mos_line[32], stats_line[64], timer_line[48];
    if (valid & BMS_UI_VALID_MOS_TEMP)
        snprintf(mos_line, sizeof mos_line, "MOS %.1f C", d->mos_temp_deci_c / 10.0);
    else snprintf(mos_line, sizeof mos_line, "MOS --");
    if ((valid & BMS_UI_VALID_CYCLES) && (valid & BMS_UI_VALID_CELLS))
        snprintf(stats_line, sizeof stats_line, "Cycles %lu     Delta %u mV",
                 (unsigned long)d->cycle_count, (unsigned)d->cell_delta_mv);
    else if (valid & BMS_UI_VALID_CYCLES)
        snprintf(stats_line, sizeof stats_line, "Cycles %lu     Delta --",
                 (unsigned long)d->cycle_count);
    else if (valid & BMS_UI_VALID_CELLS)
        snprintf(stats_line, sizeof stats_line, "Cycles --     Delta %u mV",
                 (unsigned)d->cell_delta_mv);
    else snprintf(stats_line, sizeof stats_line, "Cycles --     Delta --");
    if (valid & BMS_UI_VALID_EMERG_TIMER)
        snprintf(timer_line, sizeof timer_line, "Emergency timer %lu s",
                 (unsigned long)d->emergency_timer_s);
    else snprintf(timer_line, sizeof timer_line, "Emergency timer --");
    snprintf(thermal + thermal_len, sizeof thermal - thermal_len,
             "%s\n%s\n%s", mos_line, stats_line, timer_line);
    lv_label_set_text(s.detail_temp, thermal);

    const char *flow = "--";
    if (valid & BMS_UI_VALID_PACK_CURRENT) {
        if (d->pack_current_ma > 100) flow = "DISCHARGE";
        else if (d->pack_current_ma < -100) flow = "CHARGE / REGEN";
        else flow = "IDLE";
    }
    char balance_info[48], heater_info[64];
    if (valid & BMS_UI_VALID_BAL_CURRENT)
        snprintf(balance_info, sizeof balance_info, "Balance current %+.3f A",
                 d->balance_current_ma / 1000.0);
    else snprintf(balance_info, sizeof balance_info, "Balance current --");
    if (valid & BMS_UI_VALID_HEATER)
        snprintf(heater_info, sizeof heater_info, "Heater %s (%.3f A)",
                 d->status_flags & BMS_UI_FLAG_HEATER_ON ? "ON" : "OFF",
                 d->heater_current_ma / 1000.0);
    else snprintf(heater_info, sizeof heater_info, "Heater --");
    snprintf(text, sizeof text,
             "Power flow: %s\nCharge MOS %s | Discharge MOS %s | Balancer %s\n%s | %s",
             flow,
             (valid & BMS_UI_VALID_SWITCHES)
                 ? (d->status_flags & BMS_UI_FLAG_CHARGE_MOS_ON ? "ON" : "OFF") : "--",
             (valid & BMS_UI_VALID_SWITCHES)
                 ? (d->status_flags & BMS_UI_FLAG_DISCHARGE_MOS_ON ? "ON" : "OFF") : "--",
             (valid & BMS_UI_VALID_SWITCHES)
                 ? (d->status_flags & BMS_UI_FLAG_BALANCING ? "ON" : "OFF") : "--",
             balance_info, heater_info);
    lv_label_set_text(s.detail_state, text);

    size_t missing_len = (size_t)snprintf(text, sizeof text, "Unavailable:");
    if (!(valid & BMS_UI_VALID_WIRE_RES))
        missing_len += (size_t)snprintf(text + missing_len, sizeof text - missing_len,
                                        " wire resistance,");
    if (!(valid & BMS_UI_VALID_BAL_CURRENT))
        missing_len += (size_t)snprintf(text + missing_len, sizeof text - missing_len,
                                        " balance current,");
    if (!(valid & BMS_UI_VALID_CYCLE_CAP))
        missing_len += (size_t)snprintf(text + missing_len, sizeof text - missing_len,
                                        " cycle capacity,");
    if (!(valid & BMS_UI_VALID_HEATER))
        missing_len += (size_t)snprintf(text + missing_len, sizeof text - missing_len,
                                        " heater/current,");
    if (!(valid & BMS_UI_VALID_EMERG_TIMER))
        missing_len += (size_t)snprintf(text + missing_len, sizeof text - missing_len,
                                        " emergency timer,");
    snprintf(text + missing_len, sizeof text - missing_len,
             " sleep timer and detail log.");
    lv_label_set_text(s.unsupported, text);

    if ((valid & BMS_UI_VALID_ALARMS) && d->alarm_flags) {
        snprintf(text, sizeof text, "ALARM 0x%08lX (raw 0x%08lX)",
                 (unsigned long)d->alarm_flags, (unsigned long)d->raw_alarm_code);
        lv_obj_set_style_text_color(s.alarm, lv_color_hex(COL_DANGER), 0);
    } else {
        if (state == BMS_UI_LINK_LIVE) snprintf(text, sizeof text, "No active alarms");
        else snprintf(text, sizeof text, "%s", link_text(state));
        lv_obj_set_style_text_color(s.alarm,
                                    lv_color_hex(state == BMS_UI_LINK_LIVE ? COL_ACCENT
                                                                          : state_color), 0);
    }
    lv_label_set_text(s.alarm, text);

    bool cells_valid = (valid & BMS_UI_VALID_CELLS) != 0;
    bool wire_valid = (valid & BMS_UI_VALID_WIRE_RES) != 0;
    bool grid_has_data = s.show_wire ? (wire_valid && d->wire_res_valid_mask != 0)
                                     : (cells_valid && d->cell_valid_mask != 0);
    if (grid_has_data) lv_obj_add_flag(s.empty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(s.empty, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s.empty, s.show_wire ? "No balance-wire data" : "No cell data");

    for (uint8_t i = 0; i < BMS_UI_MAX_CELLS; ++i)
        set_cell_card(i, d, cells_valid, wire_valid);
}

#ifdef LV_REALDEVICE
static bms_ui_link_state_t map_link_state(bms_link_state_t state)
{
    switch (state) {
    case BMS_LINK_DISABLED:    return BMS_UI_LINK_DISABLED;
    case BMS_LINK_UNBOUND:     return BMS_UI_LINK_UNBOUND;
    case BMS_LINK_SCANNING:    return BMS_UI_LINK_SCANNING;
    case BMS_LINK_CONNECTING:  return BMS_UI_LINK_CONNECTING;
    case BMS_LINK_PROBING:     return BMS_UI_LINK_DISCOVERING;
    case BMS_LINK_LIVE:        return BMS_UI_LINK_LIVE;
    case BMS_LINK_STALE:       return BMS_UI_LINK_STALE;
    case BMS_LINK_UNSUPPORTED: return BMS_UI_LINK_UNSUPPORTED;
    default:                   return BMS_UI_LINK_ERROR;
    }
}

bool bms_ui_backend_get_snapshot(bms_ui_snapshot_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->abi_version = BMS_UI_ABI_VERSION;
    out->link_state = map_link_state(bms_model_link_state());
    out->age_ms = bms_model_age_ms();

    bms_snapshot_t source;
    if (!bms_model_get(&source)) {
        snprintf(out->device_name, sizeof out->device_name, "BMS not configured");
        return true;
    }

    snprintf(out->device_name, sizeof out->device_name,
             source.driver_id == BMS_DRIVER_JK_BLE ? "JK BMS" : "BLE BMS");
    snprintf(out->driver_name, sizeof out->driver_name,
             source.driver_id == BMS_DRIVER_JK_BLE ? "JK BLE" : "BLE");
    out->sample_seq = source.sample_seq;
    out->pack_mv = source.pack_mv;
    out->pack_current_ma = source.pack_current_ma;
    out->soc_permille = source.soc_permille;
    out->soh_permille = source.soh_permille;
    out->remaining_mah = source.remaining_mah > 0 ? (uint32_t)source.remaining_mah : 0;
    out->nominal_mah = source.nominal_mah > 0 ? (uint32_t)source.nominal_mah : 0;
    out->cycle_count = source.cycle_count;
    out->cell_count = source.cell_count > BMS_UI_MAX_CELLS
                          ? BMS_UI_MAX_CELLS : source.cell_count;
    memcpy(out->cell_mv, source.cell_mv,
           out->cell_count * sizeof out->cell_mv[0]);
    out->cell_valid_mask = source.cell_valid_mask;
    out->cell_min_mv = source.cell_min_mv;
    out->cell_max_mv = source.cell_max_mv;
    out->cell_delta_mv = source.cell_delta_mv;
    out->cell_min_index = source.cell_min_idx;
    out->cell_max_index = source.cell_max_idx;
    out->balancing_mask = source.balance_mask;
    if (source.valid_mask & BMS_V_WIRE_RES) {
        memcpy(out->wire_res_mohm, source.wire_res_mohm,
               sizeof out->wire_res_mohm);
        out->wire_res_valid_mask = source.wire_res_valid_mask &
                                   source.cell_valid_mask;
        if (out->wire_res_valid_mask)
            out->valid_mask |= BMS_UI_VALID_WIRE_RES;
    }
    if (source.valid_mask & BMS_V_BALANCE_CURRENT) {
        out->balance_current_ma = source.balance_current_ma;
        out->valid_mask |= BMS_UI_VALID_BAL_CURRENT;
    }
    if (source.valid_mask & BMS_V_CYCLE_CAPACITY) {
        out->cycle_capacity_mah = source.cycle_capacity_mah > 0
                                      ? (uint32_t)source.cycle_capacity_mah : 0;
        out->valid_mask |= BMS_UI_VALID_CYCLE_CAP;
    }
    if (source.valid_mask & BMS_V_HEATER) {
        out->heater_current_ma = source.heater_current_ma;
        out->valid_mask |= BMS_UI_VALID_HEATER;
        if (source.heater_on) out->status_flags |= BMS_UI_FLAG_HEATER_ON;
    }
    if (source.valid_mask & BMS_V_TIMERS) {
        out->emergency_timer_s = source.emergency_timer_s;
        out->valid_mask |= BMS_UI_VALID_EMERG_TIMER;
    }
    out->temp_count = source.temp_count > BMS_UI_MAX_TEMPS
                          ? BMS_UI_MAX_TEMPS : source.temp_count;
    memcpy(out->temp_deci_c, source.temp_deci_c,
           out->temp_count * sizeof out->temp_deci_c[0]);
    out->temp_valid_mask = source.temp_valid_mask;
    out->mos_temp_deci_c = source.mos_temp_deci_c;
    out->rssi_dbm = source.rssi_dbm;
    out->alarm_flags = source.alarm_raw;
    out->raw_alarm_code = source.alarm_raw;

    if (source.valid_mask & BMS_V_PACK_MV)
        out->valid_mask |= BMS_UI_VALID_PACK_VOLTAGE;
    if (source.valid_mask & BMS_V_CURRENT)
        out->valid_mask |= BMS_UI_VALID_PACK_CURRENT;
    if ((source.valid_mask & (BMS_V_PACK_MV | BMS_V_CURRENT)) ==
        (BMS_V_PACK_MV | BMS_V_CURRENT)) {
        int64_t power_uw = (int64_t)source.pack_mv * source.pack_current_ma;
        out->power_mw = (int32_t)(power_uw / 1000);
        out->valid_mask |= BMS_UI_VALID_POWER;
    }
    if (source.valid_mask & BMS_V_SOC)       out->valid_mask |= BMS_UI_VALID_SOC;
    if (source.valid_mask & BMS_V_SOH)       out->valid_mask |= BMS_UI_VALID_SOH;
    if ((source.valid_mask & (BMS_V_REMAINING | BMS_V_NOMINAL)) ==
        (BMS_V_REMAINING | BMS_V_NOMINAL))
        out->valid_mask |= BMS_UI_VALID_CAPACITY;
    if (source.valid_mask & BMS_V_CYCLES)    out->valid_mask |= BMS_UI_VALID_CYCLES;
    if (source.valid_mask & BMS_V_CELLS)     out->valid_mask |= BMS_UI_VALID_CELLS;
    if (source.valid_mask & BMS_V_TEMPS)     out->valid_mask |= BMS_UI_VALID_TEMPS;
    if (source.valid_mask & BMS_V_MOS_TEMP)  out->valid_mask |= BMS_UI_VALID_MOS_TEMP;
    if (source.rssi_dbm != 0)                out->valid_mask |= BMS_UI_VALID_RSSI;
    if (source.valid_mask & (BMS_V_MOSFETS | BMS_V_BALANCE)) {
        out->valid_mask |= BMS_UI_VALID_SWITCHES;
        if (source.chg_mos_on) out->status_flags |= BMS_UI_FLAG_CHARGE_MOS_ON;
        if (source.dsg_mos_on) out->status_flags |= BMS_UI_FLAG_DISCHARGE_MOS_ON;
        if (source.balancing)  out->status_flags |= BMS_UI_FLAG_BALANCING;
    }
    if (source.valid_mask & BMS_V_ALARMS)
        out->valid_mask |= BMS_UI_VALID_ALARMS;
    return true;
}

void bms_ui_backend_set_active(bool active)
{
    /* This was a no-op on the assumption that the backend streams unprompted
     * once probing finishes. It does not — the JK driver polls, so without
     * this the 1 Hz request kept running whether or not anyone had the tab
     * open, competing with Android Auto for the C6's SDIO link. The backend
     * holds the connection either way; only the polling stops. */
    ble_bms_set_active(active);
}
#else
bool bms_ui_backend_get_snapshot(bms_ui_snapshot_t *out)
{
    if (!out) return false;
    static const uint16_t cells[10] = {
        4008, 4001, 3997, 4012, 3993, 4005, 3985, 4002, 4000, 4004
    };
    static const uint16_t wire[10] = { 12, 14, 9, 17, 11, 8, 15, 10, 13, 7 };
    memset(out, 0, sizeof(*out));
    out->abi_version = BMS_UI_ABI_VERSION;
    out->sample_seq = 42;
    out->valid_mask = BMS_UI_VALID_PACK_VOLTAGE | BMS_UI_VALID_PACK_CURRENT |
                      BMS_UI_VALID_POWER | BMS_UI_VALID_SOC | BMS_UI_VALID_SOH |
                      BMS_UI_VALID_CAPACITY | BMS_UI_VALID_CYCLES |
                      BMS_UI_VALID_CELLS | BMS_UI_VALID_WIRE_RES |
                      BMS_UI_VALID_TEMPS | BMS_UI_VALID_MOS_TEMP |
                      BMS_UI_VALID_RSSI | BMS_UI_VALID_SWITCHES |
                      BMS_UI_VALID_ALARMS | BMS_UI_VALID_BAL_CURRENT |
                      BMS_UI_VALID_CYCLE_CAP | BMS_UI_VALID_HEATER |
                      BMS_UI_VALID_EMERG_TIMER;
    out->link_state = BMS_UI_LINK_LIVE;
    out->age_ms = 180;
    snprintf(out->device_name, sizeof out->device_name, "BMS-DEMO-10S");
    snprintf(out->model_name, sizeof out->model_name, "40 V pack");
    snprintf(out->driver_name, sizeof out->driver_name, "BLE demo");
    out->rssi_dbm = -58;
    out->pack_mv = 40007;
    out->pack_current_ma = 7000;
    out->power_mw = 280049;
    out->soc_permille = 784;
    out->soh_permille = 963;
    out->remaining_mah = 15680;
    out->nominal_mah = 20000;
    out->cycle_count = 126;
    out->cell_count = 10;
    memcpy(out->cell_mv, cells, sizeof cells);
    out->cell_valid_mask = (1u << 10) - 1u;
    memcpy(out->wire_res_mohm, wire, sizeof wire);
    out->wire_res_valid_mask = (1u << 10) - 1u;
    out->balance_current_ma = 550;
    out->cycle_capacity_mah = 102200;
    out->heater_current_ma = 0;
    out->emergency_timer_s = 90;
    out->cell_min_mv = 3985;
    out->cell_max_mv = 4012;
    out->cell_delta_mv = 27;
    out->cell_min_index = 6;
    out->cell_max_index = 3;
    out->balancing_mask = 1u << 3;
    out->temp_count = 2;
    out->temp_deci_c[0] = 310;
    out->temp_deci_c[1] = 321;
    out->temp_valid_mask = 0x3;
    out->mos_temp_deci_c = 337;
    out->status_flags = BMS_UI_FLAG_CHARGE_MOS_ON |
                        BMS_UI_FLAG_DISCHARGE_MOS_ON |
                        BMS_UI_FLAG_BALANCING;
    return true;
}

void bms_ui_backend_set_active(bool active)
{
    (void)active;
}
#endif

static void update_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s.root) return;
    bms_ui_snapshot_t snapshot;
    bool ok = bms_ui_backend_get_snapshot(&snapshot);
    if (ok && snapshot.abi_version != BMS_UI_ABI_VERSION) ok = false;
    if (!ok) memset(&snapshot, 0, sizeof snapshot);
    render_snapshot(&snapshot, ok);
}

void bms_view_create(lv_obj_t *parent)
{
    if (!parent || s.root) return;
    memset(&s, 0, sizeof s);

    s.root = lv_obj_create(parent);
    lv_obj_set_size(s.root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s.root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s.root, 0, 0);
    lv_obj_set_style_pad_all(s.root, 0, 0);
    lv_obj_clear_flag(s.root, LV_OBJ_FLAG_SCROLLABLE);

    s.device = label_at(s.root, "BMS", 8, 4, &lv_font_montserratMedium_16, COL_TEXT);
    s.link = label_at(s.root, "Not paired", 370, 4,
                      &lv_font_montserratMedium_16, COL_DIM);
    lv_obj_set_width(s.link, 238);
    lv_obj_set_style_text_align(s.link, LV_TEXT_ALIGN_RIGHT, 0);
    s.diagnostic = label_at(s.root, "BLE | No sample", 8, 23,
                            &lv_font_montserratMedium_12, COL_DIM);

#ifdef LV_REALDEVICE
    s.pair = lv_btn_create(s.root);
    lv_obj_set_pos(s.pair, 616, 2);
    lv_obj_set_size(s.pair, 64, 34);
    lv_obj_set_style_bg_color(s.pair, lv_color_hex(COL_CYAN), 0);
    lv_obj_set_style_radius(s.pair, 5, 0);
    lv_obj_t *pair_label = lv_label_create(s.pair);
    lv_label_set_text(pair_label, "PAIR");
    lv_obj_set_style_text_font(pair_label, &lv_font_montserratMedium_12, 0);
    lv_obj_center(pair_label);
    lv_obj_add_event_cb(s.pair, pair_cb, LV_EVENT_CLICKED, NULL);

    s.forget = lv_btn_create(s.root);
    lv_obj_set_pos(s.forget, 686, 2);
    lv_obj_set_size(s.forget, 66, 34);
    lv_obj_set_style_bg_color(s.forget, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_radius(s.forget, 5, 0);
    lv_obj_t *forget_label = lv_label_create(s.forget);
    lv_label_set_text(forget_label, "FORGET");
    lv_obj_set_style_text_font(forget_label, &lv_font_montserratMedium_11, 0);
    lv_obj_center(forget_label);
    lv_obj_add_event_cb(s.forget, forget_cb, LV_EVENT_CLICKED, NULL);
#endif

    /* Keep connection controls fixed while the telemetry body scrolls. The
     * tab content is 374 px high on the 800x480 target, leaving 332 px below
     * this 42 px header. Children below y=332 extend the vertical scroll
     * range automatically. */
    s.scroll = lv_obj_create(s.root);
    lv_obj_set_pos(s.scroll, 0, 42);
    lv_obj_set_size(s.scroll, lv_pct(100), 332);
    lv_obj_set_style_bg_color(s.scroll, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s.scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s.scroll, 0, 0);
    lv_obj_set_style_radius(s.scroll, 0, 0);
    lv_obj_set_style_pad_all(s.scroll, 0, 0);
    lv_obj_set_scroll_dir(s.scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s.scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s.scroll, lv_color_hex(COL_CYAN), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s.scroll, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s.scroll, 5, LV_PART_SCROLLBAR);

    lv_obj_t *left = panel_create(s.scroll, 4, 0, 218, 288);
    s.soc_arc = lv_arc_create(left);
    lv_obj_set_pos(s.soc_arc, 44, 8);
    lv_obj_set_size(s.soc_arc, 130, 130);
    lv_arc_set_range(s.soc_arc, 0, 1000);
    lv_arc_set_rotation(s.soc_arc, 135);
    lv_arc_set_bg_angles(s.soc_arc, 0, 270);
    lv_obj_remove_style(s.soc_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s.soc_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s.soc_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s.soc_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s.soc_arc, lv_color_hex(COL_BTN), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s.soc_arc, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);

    s.soc = label_at(left, "--%", 0, 52, &lv_font_montserratMedium_26, COL_TEXT);
    lv_obj_set_width(s.soc, 218);
    lv_obj_set_style_text_align(s.soc, LV_TEXT_ALIGN_CENTER, 0);
    s.pack = label_at(left, "Pack   --", 16, 139, &lv_font_montserratMedium_16, COL_TEXT);
    s.current = label_at(left, "Current   --", 16, 162,
                         &lv_font_montserratMedium_16, COL_TEXT);
    s.power = label_at(left, "Power   --", 16, 185, &lv_font_montserratMedium_16, COL_TEXT);
    s.thermal = label_at(left, "Battery --   MOS --", 16, 214,
                         &lv_font_montserratMedium_12, COL_DIM);
    s.health = label_at(left, "SOH --   Cycles --", 16, 234,
                        &lv_font_montserratMedium_12, COL_DIM);
    s.switches = label_at(left, "CHG -- | DSG -- | BAL --", 16, 254,
                          &lv_font_montserratMedium_12, COL_DIM);

    lv_obj_t *right = panel_create(s.scroll, 226, 0, 526, 288);
    static const char *mode_map[] = { "CELLS", "WIRE", "" };
    s.mode = lv_btnmatrix_create(right);
    lv_btnmatrix_set_map(s.mode, mode_map);
    lv_btnmatrix_set_one_checked(s.mode, true);
    lv_btnmatrix_set_btn_ctrl(s.mode, 0, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_set_pos(s.mode, 8, 7);
    lv_obj_set_size(s.mode, 174, 30);
    lv_obj_set_style_bg_color(s.mode, lv_color_hex(COL_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s.mode, lv_color_hex(COL_CARD), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s.mode, lv_color_hex(COL_CYAN),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s.mode, lv_color_hex(COL_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(s.mode, &lv_font_montserratMedium_12, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s.mode, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s.mode, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(s.mode, mode_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s.cell_summary = label_at(right, "Cells  --", 190, 14,
                              &lv_font_montserratMedium_12, COL_DIM);
    lv_obj_set_width(s.cell_summary, 325);
    lv_obj_set_style_text_align(s.cell_summary, LV_TEXT_ALIGN_RIGHT, 0);

    for (uint8_t i = 0; i < BMS_UI_MAX_CELLS; ++i) {
        int col = i % 4;
        int row = i / 4;
        lv_obj_t *card = lv_obj_create(right);
        lv_obj_set_pos(card, 8 + col * 127, 43 + row * 30);
        lv_obj_set_size(card, 119, 26);
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 5, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        s.cell_card[i] = card;
        s.cell_num[i] = label_at(card, "--", 5, 5, &lv_font_montserratMedium_12, COL_DIM);
        s.cell_value[i] = label_at(card, "--", 31, 5,
                                   &lv_font_montserratMedium_12, COL_TEXT);
        lv_obj_set_width(s.cell_value[i], 83);
        lv_obj_set_style_text_align(s.cell_value[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    }

    s.empty = label_at(right, "No cell data", 0, 145,
                       &lv_font_montserratMedium_16, COL_DIM);
    lv_obj_set_width(s.empty, 526);
    lv_obj_set_style_text_align(s.empty, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *details = panel_create(s.scroll, 4, 296, 748, 176);
    label_at(details, "PACK DETAILS", 12, 9,
             &lv_font_montserratMedium_16, COL_ACCENT);
    s.detail_pack = label_at(details, "Cell average --\nCapacity --\nRemaining --\nUsed capacity --",
                             16, 36, &lv_font_montserratMedium_12, COL_TEXT);
    lv_obj_set_width(s.detail_pack, 330);
    lv_obj_set_style_text_line_space(s.detail_pack, 7, 0);
    s.detail_temp = label_at(details, "T1 --     T2 --\nMOS --\nCycles --     Delta --",
                             390, 36, &lv_font_montserratMedium_12, COL_TEXT);
    lv_obj_set_width(s.detail_temp, 340);
    lv_obj_set_style_text_line_space(s.detail_temp, 7, 0);

    lv_obj_t *status = panel_create(s.scroll, 4, 480, 748, 126);
    label_at(status, "BMS STATUS", 12, 9,
             &lv_font_montserratMedium_16, COL_ACCENT);
    s.detail_state = label_at(status,
                              "Power flow: --\nCharge MOS -- | Discharge MOS -- | Balancer --",
                              16, 35, &lv_font_montserratMedium_12, COL_TEXT);
    lv_obj_set_width(s.detail_state, 716);
    lv_obj_set_style_text_line_space(s.detail_state, 5, 0);
    s.alarm = label_at(status, "Not paired", 16, 99,
                       &lv_font_montserratMedium_12, COL_DIM);
    lv_obj_set_width(s.alarm, 716);
    lv_obj_set_style_text_align(s.alarm, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *unsupported = panel_create(s.scroll, 4, 614, 748, 82);
    label_at(unsupported, "ADDITIONAL JK DATA", 12, 9,
             &lv_font_montserratMedium_16, COL_ACCENT);
    s.unsupported = label_at(unsupported,
        "Unavailable: wire resistance, balance current, cycle capacity, "
        "heater/current, emergency/sleep timers and detail log.",
        16, 36, &lv_font_montserratMedium_11, COL_DIM);
    lv_obj_set_width(s.unsupported, 716);
    lv_label_set_long_mode(s.unsupported, LV_LABEL_LONG_WRAP);

    s.timer = lv_timer_create(update_cb, 500, NULL);
    update_cb(s.timer);
}

void bms_view_set_active(bool active)
{
    if (s.active == active) return;
    s.active = active;
    bms_ui_backend_set_active(active);
    if (active && s.timer) update_cb(s.timer);
}

void bms_view_destroy(void)
{
#ifdef LV_REALDEVICE
    ble_bms_set_scan_cb(NULL);
    ble_bms_scan_stop();
#endif
    if (s.active) bms_ui_backend_set_active(false);
    if (s.timer) lv_timer_del(s.timer);
    memset(&s, 0, sizeof s);
}
