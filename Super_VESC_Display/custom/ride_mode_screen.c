/* Ride-mode and reverse settings frontend.
 *
 * This screen stages edits locally and sends one atomic configuration only
 * when Save is pressed. All movement/throttle/reverse validation remains in
 * the VESC Lisp backend; FE result messages merely explain its decision.
 */
#include "lvgl.h"
#include "custom.h"
#include "vesc_can/vesc_ride_mode.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern lv_ui guider_ui;

#define COL_BG      0x07090A
#define COL_PANEL   0x12181C
#define COL_BTN     0x2A3440
#define COL_ACCENT  0xB6FF2E
#define COL_CYAN    0x00A9FF
#define COL_RED     0xFF3B30
#define COL_AMBER   0xFFA500
#define COL_TEXT    0xFFFFFF
#define COL_DIM     0x8A9499

#define SAVE_TIMEOUT_MS 5000u

typedef enum {
    VALUE_SPEED_DKMH,
    VALUE_CURRENT_PM,
    VALUE_CURRENT_DA,
} value_kind_t;

typedef struct {
    uint16_t    *value;
    uint16_t     vmin;
    uint16_t     vmax;
    uint16_t     step;
    value_kind_t kind;
    lv_obj_t    *label;
} value_editor_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_footer_status;
static lv_obj_t *s_active_status;
static lv_obj_t *s_reverse_enabled;
static lv_obj_t *s_reverse_button_status;
static lv_obj_t *s_reverse_arm_status;
static lv_obj_t *s_direction_status;
static lv_timer_t *s_timer;

static value_editor_t s_editors[8];
static uint8_t s_editor_count;
static vesc_ride_config_t s_edit;
static uint32_t s_save_started_ms;
static uint16_t s_pending_seq;
static bool s_alive;
static bool s_have_config;
static bool s_dirty;
static bool s_save_waiting;
static uint32_t s_seen_config_epoch;
static uint32_t s_seen_status_epoch;

static void footer_message(const char *text, uint32_t color)
{
    if (!s_footer_status) return;
    lv_label_set_text(s_footer_status, text);
    lv_obj_set_style_text_color(s_footer_status, lv_color_hex(color), 0);
}

static void set_defaults(void)
{
    memset(&s_edit, 0, sizeof(s_edit));
    s_edit.speed_dkmh[0] = 50;
    s_edit.speed_dkmh[1] = 100;
    s_edit.speed_dkmh[2] = 200;
    s_edit.current_permille[0] = 300;
    s_edit.current_permille[1] = 600;
    s_edit.current_permille[2] = 1000;
    s_edit.reverse_speed_dkmh = 30;
    s_edit.reverse_current_dA = 70;
}

static void editor_refresh(value_editor_t *ed)
{
    if (!ed || !ed->label || !ed->value) return;
    uint16_t value = *ed->value;
    char text[32];
    switch (ed->kind) {
    case VALUE_SPEED_DKMH:
        snprintf(text, sizeof(text), "%u.%u km/h", value / 10u, value % 10u);
        break;
    case VALUE_CURRENT_PM:
        snprintf(text, sizeof(text), "%u%%", value / 10u);
        break;
    case VALUE_CURRENT_DA:
        snprintf(text, sizeof(text), "%u.%u A", value / 10u, value % 10u);
        break;
    default:
        snprintf(text, sizeof(text), "%u", value);
        break;
    }
    lv_label_set_text(ed->label, text);
}

static void refresh_editors(void)
{
    for (uint8_t i = 0; i < s_editor_count; ++i) editor_refresh(&s_editors[i]);
    if (s_reverse_enabled) {
        if (s_edit.reverse_enabled) {
            lv_obj_add_state(s_reverse_enabled, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_reverse_enabled, LV_STATE_CHECKED);
        }
    }
}

static void mark_dirty(void)
{
    s_dirty = true;
    s_save_waiting = false;
    footer_message("Unsaved changes", COL_AMBER);
}

static void editor_minus_cb(lv_event_t *e)
{
    value_editor_t *ed = lv_event_get_user_data(e);
    if (!ed || !ed->value) return;
    uint16_t value = *ed->value;
    *ed->value = value > ed->vmin + ed->step - 1u
                     ? (uint16_t)(value - ed->step)
                     : ed->vmin;
    editor_refresh(ed);
    mark_dirty();
}

static void editor_plus_cb(lv_event_t *e)
{
    value_editor_t *ed = lv_event_get_user_data(e);
    if (!ed || !ed->value) return;
    uint32_t value = (uint32_t)*ed->value + ed->step;
    *ed->value = value < ed->vmax ? (uint16_t)value : ed->vmax;
    editor_refresh(ed);
    mark_dirty();
}

static lv_obj_t *make_step_button(lv_obj_t *parent, const char *text,
                                  uint32_t color, lv_event_cb_t cb,
                                  void *user_data)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 58, 42);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_radius(button, 7, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_add_event_cb(button, cb, LV_EVENT_LONG_PRESSED_REPEAT, user_data);
    return button;
}

static lv_obj_t *make_row(lv_obj_t *parent, const char *title, int height)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, height);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (title) {
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, title);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserratMedium_16, 0);
    }
    return row;
}

static value_editor_t *add_value_editor(lv_obj_t *parent, const char *title,
                                        uint16_t *value, uint16_t vmin,
                                        uint16_t vmax, uint16_t step,
                                        value_kind_t kind)
{
    if (s_editor_count >= sizeof(s_editors) / sizeof(s_editors[0])) return NULL;
    value_editor_t *ed = &s_editors[s_editor_count++];
    *ed = (value_editor_t){
        .value = value, .vmin = vmin, .vmax = vmax, .step = step, .kind = kind,
    };

    lv_obj_t *row = make_row(parent, title, 54);
    lv_obj_t *minus = make_step_button(row, "-", COL_RED, editor_minus_cb, ed);
    lv_obj_align(minus, LV_ALIGN_RIGHT_MID, -206, 0);

    ed->label = lv_label_create(row);
    lv_obj_set_width(ed->label, 136);
    lv_obj_align(ed->label, LV_ALIGN_RIGHT_MID, -68, 0);
    lv_obj_set_style_text_align(ed->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ed->label, lv_color_hex(COL_CYAN), 0);
    lv_obj_set_style_text_font(ed->label, &lv_font_montserrat_24, 0);

    lv_obj_t *plus = make_step_button(row, "+", COL_CYAN, editor_plus_cb, ed);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -4, 0);
    editor_refresh(ed);
    return ed;
}

static void section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserratMedium_16, 0);
    lv_obj_set_style_pad_top(label, 6, 0);
}

static void select_mode_cb(lv_event_t *e)
{
    uint8_t profile = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    uint16_t seq = 0;
    if (!vesc_ride_mode_select(profile, &seq)) {
        footer_message("Ride mode backend unavailable", COL_RED);
        return;
    }
    footer_message("Mode request sent", COL_CYAN);
}

static void add_mode_tab(lv_obj_t *tab, uint8_t profile)
{
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(tab, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_AUTO);

    char title[24];
    snprintf(title, sizeof(title), "Mode %u limits", profile + 1u);
    section_label(tab, title);
    add_value_editor(tab, "Speed limit", &s_edit.speed_dkmh[profile],
                     VESC_RIDE_FORWARD_SPEED_MIN_DKMH,
                     VESC_RIDE_FORWARD_SPEED_MAX_DKMH, 10,
                     VALUE_SPEED_DKMH);
    add_value_editor(tab, "Motor current", &s_edit.current_permille[profile],
                     VESC_RIDE_FORWARD_CURRENT_MIN_PM,
                     VESC_RIDE_FORWARD_CURRENT_MAX_PM, 50,
                     VALUE_CURRENT_PM);

    lv_obj_t *row = make_row(tab, NULL, 50);
    lv_obj_t *button = lv_btn_create(row);
    lv_obj_set_size(button, 190, 42);
    lv_obj_align(button, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_radius(button, 7, 0);
    lv_obj_t *label = lv_label_create(button);
    snprintf(title, sizeof(title), "Use Mode %u", profile + 1u);
    lv_label_set_text(label, title);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, select_mode_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)profile);

    lv_obj_t *hint = lv_label_create(tab);
    lv_label_set_text(hint,
        "Speed is a soft ESC limit. Current controls available torque below it.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
}

static void reverse_switch_cb(lv_event_t *e)
{
    s_edit.reverse_enabled = lv_obj_has_state(lv_event_get_target(e),
                                               LV_STATE_CHECKED);
    mark_dirty();
}

static lv_obj_t *add_readout(lv_obj_t *parent, const char *title)
{
    lv_obj_t *row = make_row(parent, title, 36);
    lv_obj_t *value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(COL_DIM), 0);
    return value;
}

static void add_reverse_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(tab, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_AUTO);

    section_label(tab, "Reverse limits");
    lv_obj_t *enable_row = make_row(tab, "Enable reverse", 48);
    s_reverse_enabled = lv_switch_create(enable_row);
    lv_obj_set_size(s_reverse_enabled, 60, 30);
    lv_obj_align(s_reverse_enabled, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(s_reverse_enabled, lv_color_hex(COL_BTN),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_reverse_enabled, lv_color_hex(COL_CYAN),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_reverse_enabled, reverse_switch_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    add_value_editor(tab, "Reverse speed", &s_edit.reverse_speed_dkmh,
                     VESC_RIDE_REVERSE_SPEED_MIN_DKMH,
                     VESC_RIDE_REVERSE_SPEED_MAX_DKMH, 10,
                     VALUE_SPEED_DKMH);
    add_value_editor(tab, "Reverse current", &s_edit.reverse_current_dA,
                     VESC_RIDE_REVERSE_CURRENT_MIN_DA,
                     VESC_RIDE_REVERSE_CURRENT_MAX_DA, 5,
                     VALUE_CURRENT_DA);

    section_label(tab, "Physical input");
    s_reverse_button_status = add_readout(tab, "R button");
    s_reverse_arm_status = add_readout(tab, "Interlock");
    s_direction_status = add_readout(tab, "Direction");

    lv_obj_t *warning = lv_label_create(tab);
    lv_label_set_text(warning,
        "Hold-to-run only. Reverse is armed by the ESC after stop, released "
        "throttle and brake confirmation.");
    lv_label_set_long_mode(warning, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(warning, lv_pct(100));
    lv_obj_set_style_text_color(warning, lv_color_hex(COL_AMBER), 0);
    lv_obj_set_style_text_font(warning, &lv_font_montserrat_14, 0);
}

static const char *result_text(uint8_t result)
{
    switch (result) {
    case VESC_RIDE_RESULT_OK:                    return "Saved on VESC";
    case VESC_RIDE_RESULT_BAD_LENGTH:            return "Invalid backend packet";
    case VESC_RIDE_RESULT_BAD_VERSION:           return "Backend version mismatch";
    case VESC_RIDE_RESULT_OUT_OF_RANGE:           return "Value outside safe range";
    case VESC_RIDE_RESULT_ORDER_INVALID:          return "Mode speeds must increase";
    case VESC_RIDE_RESULT_VEHICLE_MOVING:         return "Stop vehicle before saving";
    case VESC_RIDE_RESULT_THROTTLE_NOT_RELEASED:  return "Release throttle before saving";
    case VESC_RIDE_RESULT_REVERSE_ACTIVE:         return "Exit reverse before saving";
    case VESC_RIDE_RESULT_STORAGE_ERROR:          return "VESC storage error";
    case VESC_RIDE_RESULT_UNSUPPORTED_HARDWARE:   return "Reverse input unsupported";
    case VESC_RIDE_RESULT_TIMEOUT:                return "VESC response timeout";
    default:                                      return "Ride mode update rejected";
    }
}

static bool local_config_valid(void)
{
    return s_edit.speed_dkmh[0] <= s_edit.speed_dkmh[1] &&
           s_edit.speed_dkmh[1] <= s_edit.speed_dkmh[2];
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    if (!s_have_config) {
        footer_message("Ride mode backend unavailable", COL_RED);
        return;
    }
    if (!local_config_valid()) {
        footer_message("Mode speeds must increase", COL_RED);
        return;
    }

    uint16_t seq = 0;
    if (!vesc_ride_mode_set_config(&s_edit, &seq)) {
        footer_message("Could not queue Save", COL_RED);
        return;
    }
    s_pending_seq = seq;
    s_save_started_ms = lv_tick_get();
    s_save_waiting = true;
    footer_message("Saving to VESC...", COL_CYAN);
}

static void load_cb(lv_event_t *e)
{
    (void)e;
    vesc_ride_config_t cfg;
    if (vesc_ride_mode_get_config(&cfg) && cfg.valid) {
        s_edit = cfg;
        s_have_config = true;
        s_dirty = false;
        refresh_editors();
    }
    if (!vesc_ride_mode_request_config()) {
        footer_message("Ride mode backend unavailable", COL_RED);
    } else {
        footer_message("Reading from VESC...", COL_CYAN);
    }
}

static void update_live_status(const vesc_ride_status_t *status)
{
    if (!status || !status->valid) return;
    char text[48];
    snprintf(text, sizeof(text), "Active M%u - %u.%u km/h",
             status->current_profile + 1u,
             status->active_speed_dkmh / 10u,
             status->active_speed_dkmh % 10u);
    lv_label_set_text(s_active_status, text);

    if (s_reverse_button_status) {
        lv_label_set_text(s_reverse_button_status,
                          status->reverse_button ? "PRESSED" : "released");
        lv_obj_set_style_text_color(s_reverse_button_status,
            lv_color_hex(status->reverse_button ? COL_AMBER : COL_DIM), 0);
    }
    if (s_reverse_arm_status) {
        lv_label_set_text(s_reverse_arm_status,
                          status->reverse_armed ? "ARMED" : "safe");
        lv_obj_set_style_text_color(s_reverse_arm_status,
            lv_color_hex(status->reverse_armed ? COL_RED : COL_ACCENT), 0);
    }
    if (s_direction_status) {
        const char *direction = status->direction_state < 0 ? "REVERSE" :
                                status->direction_state > 0 ? "forward" : "coast";
        uint32_t color = status->direction_state < 0 ? COL_RED :
                         status->direction_state > 0 ? COL_ACCENT : COL_DIM;
        lv_label_set_text(s_direction_status, direction);
        lv_obj_set_style_text_color(s_direction_status, lv_color_hex(color), 0);
    }
}

static void poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_alive) return;

    vesc_ride_config_t cfg;
    if (vesc_ride_mode_get_config(&cfg)) {
        bool changed = cfg.epoch != s_seen_config_epoch;
        s_seen_config_epoch = cfg.epoch;

        if (!cfg.valid) {
            if (changed || !s_have_config) {
                footer_message(result_text(cfg.last_result), COL_RED);
            }
        } else {
            bool first = !s_have_config;
            bool matching_save = s_save_waiting &&
                                 cfg.response_seq == s_pending_seq;
            s_have_config = true;

            if (first || (changed && (!s_dirty || matching_save))) {
                s_edit = cfg;
                refresh_editors();
            }
            if (matching_save) {
                s_save_waiting = false;
                s_dirty = false;
                if (cfg.last_result == VESC_RIDE_RESULT_OK) {
                    footer_message(cfg.persist_pending
                                       ? "Applied; saving EEPROM..."
                                       : "Saved on VESC",
                                   cfg.persist_pending ? COL_AMBER : COL_ACCENT);
                } else {
                    footer_message(result_text(cfg.last_result), COL_RED);
                }
            } else if (!s_dirty && cfg.persist_pending) {
                footer_message("Applied; saving EEPROM...", COL_AMBER);
            }
        }
    }

    vesc_ride_status_t status;
    if (vesc_ride_mode_get_status(&status) && status.valid &&
        status.epoch != s_seen_status_epoch) {
        s_seen_status_epoch = status.epoch;
        update_live_status(&status);
    }

    if (s_save_waiting &&
        (uint32_t)(lv_tick_get() - s_save_started_ms) > SAVE_TIMEOUT_MS) {
        s_save_waiting = false;
        footer_message("VESC response timeout", COL_RED);
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    lv_scr_load_anim(guider_ui.settings, LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                     200, 0, false);
}

static void screen_unloaded_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCREEN_UNLOADED) return;
    s_alive = false;
    vesc_ride_mode_set_screen_active(false);
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    if (s_screen) {
        lv_obj_del_async(s_screen);
        s_screen = NULL;
    }
    s_footer_status = NULL;
    s_active_status = NULL;
    s_reverse_enabled = NULL;
    s_reverse_button_status = NULL;
    s_reverse_arm_status = NULL;
    s_direction_status = NULL;
}

static lv_obj_t *footer_button(lv_obj_t *parent, const char *text, int x,
                               int width, uint32_t color, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, 422);
    lv_obj_set_size(button, width, 48);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_radius(button, 7, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    return button;
}

void show_ride_mode_settings(void)
{
    if (s_screen) return;

    set_defaults();
    memset(s_editors, 0, sizeof(s_editors));
    s_editor_count = 0;
    s_pending_seq = 0;
    s_seen_config_epoch = 0;
    s_seen_status_epoch = 0;
    s_have_config = false;
    s_dirty = false;
    s_save_waiting = false;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, 800, 480);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(s_screen);
    lv_obj_set_pos(back, 8, 8);
    lv_obj_set_size(back, 90, 40);
    lv_obj_set_style_bg_color(back, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_radius(back, 7, 0);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Ride Modes");
    lv_obj_set_pos(title, 110, 16);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    s_active_status = lv_label_create(s_screen);
    lv_label_set_text(s_active_status, "Active mode --");
    lv_obj_set_pos(s_active_status, 410, 18);
    lv_obj_set_width(s_active_status, 380);
    lv_obj_set_style_text_align(s_active_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_active_status, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_text_font(s_active_status, &lv_font_montserratMedium_16, 0);

    lv_obj_t *tabs = lv_tabview_create(s_screen, LV_DIR_TOP, 42);
    lv_obj_set_pos(tabs, 8, 56);
    lv_obj_set_size(tabs, 784, 356);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_t *tab_buttons = lv_tabview_get_tab_btns(tabs);
    lv_obj_set_style_bg_color(tab_buttons, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_color(tab_buttons, lv_color_hex(COL_DIM),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(tab_buttons, lv_color_hex(COL_ACCENT),
                                LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_t *mode1 = lv_tabview_add_tab(tabs, "MODE 1");
    lv_obj_t *mode2 = lv_tabview_add_tab(tabs, "MODE 2");
    lv_obj_t *mode3 = lv_tabview_add_tab(tabs, "MODE 3");
    lv_obj_t *reverse = lv_tabview_add_tab(tabs, "REVERSE");
    add_mode_tab(mode1, 0);
    add_mode_tab(mode2, 1);
    add_mode_tab(mode3, 2);
    add_reverse_tab(reverse);

    footer_button(s_screen, "Reload", 8, 126, COL_BTN, load_cb);
    s_footer_status = lv_label_create(s_screen);
    lv_label_set_text(s_footer_status, "Loading from VESC...");
    lv_obj_set_pos(s_footer_status, 146, 435);
    lv_obj_set_width(s_footer_status, 470);
    lv_obj_set_style_text_align(s_footer_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_footer_status, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(s_footer_status, &lv_font_montserratMedium_16, 0);
    footer_button(s_screen, "Save", 628, 164, COL_CYAN, save_cb);

    refresh_editors();
    lv_obj_add_event_cb(s_screen, screen_unloaded_cb,
                        LV_EVENT_SCREEN_UNLOADED, NULL);
    s_alive = true;
    vesc_ride_mode_set_screen_active(true);
    if (!vesc_ride_mode_request_config()) {
        footer_message("Ride mode backend unavailable", COL_RED);
    }
    s_timer = lv_timer_create(poll_cb, 200, NULL);
    poll_cb(s_timer);

    lv_scr_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
