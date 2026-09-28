/* Actual production panel + LVGL event processing, no mocked click engine. */
#include "lvgl.h"
#include "custom.h"
#include "vesc_can/vesc_lisp_panel.h"
#include "vesc_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", test_name, __LINE__, #condition); \
    return 1; } } while (0)

static const char *test_name;
static lv_obj_t *dashboard;
static bool model_available = true;
static bool polling_enabled;
static vlp_model_t model;
static lv_indev_state_t pointer_state = LV_INDEV_STATE_RELEASED;
static lv_point_t pointer = {10, 240};
static struct { uint8_t id; float value; } actions[32];
static unsigned action_count;

lv_obj_t *vesc_ui_get_screen(void) { return dashboard; }
void vesc_lisp_panel_set_enabled(bool enabled) { polling_enabled = enabled; }
bool vesc_lisp_panel_get_model(vlp_model_t *out)
{
    if (!model_available) return false;
    *out = model;
    return true;
}
void vesc_lisp_panel_send_action(uint8_t id, float value)
{
    if (action_count >= 32) abort();
    actions[action_count].id = id;
    actions[action_count++].value = value;
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    (void)area; (void)pixels;
    lv_disp_flush_ready(driver);
}
static void read_pointer(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    data->point = pointer;
    data->state = pointer_state;
}
static void advance(unsigned ms)
{
    for (unsigned i = 0; i < ms; i += 5) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
}
static void touch(int x, int y, lv_indev_state_t state, unsigned ms)
{
    pointer.x = x; pointer.y = y; pointer_state = state;
    advance(ms);
}
static void tap(int x, int y)
{
    touch(x, y, LV_INDEV_STATE_PRESSED, 30);
    touch(x, y, LV_INDEV_STATE_RELEASED, 30);
}
static void open_with_held_swipe(int release_x, int release_y)
{
    touch(10, 240, LV_INDEV_STATE_PRESSED, 30);
    touch(135, 240, LV_INDEV_STATE_PRESSED, 20);
    /* Same entry point as the edge-swipe callback. It executes outside the
     * indev read callback, so lv_indev_get_act() cannot identify the pointer. */
    lisp_panel_open_async();
    advance(15);
    touch(release_x, release_y, LV_INDEV_STATE_PRESSED, 300);
    touch(release_x, release_y, LV_INDEV_STATE_RELEASED, 300);
}
static unsigned overlay_count(void)
{
    return lv_obj_get_child_cnt(lv_layer_top());
}
static lv_obj_t *find_type(lv_obj_t *parent, const lv_obj_class_t *type,
                           unsigned *skip)
{
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(child, type)) {
            if (*skip == 0) return child;
            --*skip;
        }
        lv_obj_t *found = find_type(child, type, skip);
        if (found) return found;
    }
    return NULL;
}
static lv_obj_t *control(const lv_obj_class_t *type, unsigned index)
{
    return find_type(lv_layer_top(), type, &index);
}
static void tap_object(lv_obj_t *obj)
{
    lv_obj_update_layout(obj);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
}
static void init(void)
{
    lv_init();
    static lv_color_t pixels[800 * 16];
    static lv_disp_draw_buf_t draw;
    static lv_disp_drv_t display;
    lv_disp_draw_buf_init(&draw, pixels, NULL, 800 * 16);
    lv_disp_drv_init(&display);
    display.hor_res = 800; display.ver_res = 480;
    display.draw_buf = &draw; display.flush_cb = flush;
    lv_disp_drv_register(&display);
    static lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER; input.read_cb = read_pointer;
    lv_indev_drv_register(&input);
    dashboard = lv_scr_act();
    /* Generated dashboard root is clickable and has no PRESS_LOCK. */
    lv_obj_add_flag(dashboard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dashboard, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_PRESS_LOCK);
    model.version = 1; model.count = 4;
    model.ui_epoch = 1; model.state_epoch = 1;
    model.ctrl[0] = (vlp_ctrl_t){ .id = 6, .type = VLP_CTRL_BUTTON };
    strcpy(model.ctrl[0].label, "Park");
    for (unsigned i = 1; i < 4; ++i) {
        model.ctrl[i].id = 9 + i;
        model.ctrl[i].type = VLP_CTRL_TOGGLE;
        model.ctrl[i].value = (i == 1);
        snprintf(model.ctrl[i].label, VLP_LABEL_MAX, "Mode %u", i);
    }
    advance(30);
}

static int run(void)
{
    if (!strcmp(test_name, "opening_release_outside")) {
        open_with_held_swipe(420, 240);
        CHECK(polling_enabled); CHECK(overlay_count() == 2);
        CHECK(action_count == 0);
    } else if (!strcmp(test_name, "opening_release_inside")) {
        open_with_held_swipe(240, 150);
        CHECK(polling_enabled); CHECK(overlay_count() == 2);
        CHECK(action_count == 0);
    } else if (!strcmp(test_name, "next_outside_tap")) {
        open_with_held_swipe(420, 240);
        CHECK(polling_enabled);
        tap(600, 240); advance(300);
        CHECK(!polling_enabled); CHECK(overlay_count() == 0);
    } else if (!strcmp(test_name, "mode_and_park_actions")) {
        show_lisp_panel(); advance(300);
        CHECK(control(&lv_switch_class, 2) != NULL);
        const unsigned order[] = {1, 2, 0};
        for (unsigned i = 0; i < 3; ++i) {
            unsigned index = order[i];
            lv_obj_t *sw = control(&lv_switch_class, index);
            CHECK(sw != NULL);
            tap_object(sw);
            CHECK(action_count == i + 1);
            CHECK(actions[i].id == 10 + index);
            CHECK(actions[i].value == (index == 0 ? 0.0f : 1.0f));
            CHECK(polling_enabled);
        }
        lv_obj_t *button = control(&lv_btn_class, 0);
        CHECK(button != NULL); tap_object(button);
        CHECK(action_count == 4); CHECK(actions[3].id == 6);
        CHECK(actions[3].value == 1.0f); CHECK(polling_enabled);
        /* Incoming state refresh must not send any extra action. */
        model.ctrl[1].value = 0; model.ctrl[2].value = 1;
        ++model.state_epoch; advance(250);
        CHECK(action_count == 4);
        CHECK(lv_obj_has_state(control(&lv_switch_class, 1), LV_STATE_CHECKED));
    } else if (!strcmp(test_name, "missing_descriptor") ||
               !strcmp(test_name, "late_descriptor")) {
        model_available = false;
        open_with_held_swipe(420, 240); advance(1000);
        CHECK(polling_enabled); CHECK(overlay_count() == 2);
        CHECK(control(&lv_switch_class, 0) == NULL);
        if (!strcmp(test_name, "late_descriptor")) {
            model_available = true; advance(250);
            CHECK(control(&lv_switch_class, 2) != NULL);
            CHECK(polling_enabled); CHECK(action_count == 0);
        }
    } else if (!strcmp(test_name, "dashboard_only")) {
        lv_scr_load(lv_obj_create(NULL)); advance(20);
        lisp_panel_open_async(); advance(300);
        CHECK(!polling_enabled); CHECK(overlay_count() == 0);
    } else if (!strcmp(test_name, "screen_change")) {
        show_lisp_panel(); advance(300);
        CHECK(polling_enabled);
        lv_scr_load(lv_obj_create(NULL)); advance(500);
        CHECK(!polling_enabled); CHECK(overlay_count() == 0);
    } else if (!strcmp(test_name, "reopen_during_close")) {
        show_lisp_panel(); advance(300);
        lisp_panel_close(); advance(50);
        lisp_panel_open_async(); advance(500);
        /* Ignore opening until old drawer is torn down, or safely replace it;
         * in either case no orphaned overlay or live poll with deleted rows. */
        CHECK(overlay_count() == (polling_enabled ? 2 : 0));
        if (!polling_enabled) { show_lisp_panel(); advance(300); }
        CHECK(polling_enabled); CHECK(overlay_count() == 2);
        CHECK(control(&lv_switch_class, 2) != NULL);
        lisp_panel_close(); advance(300);
        CHECK(!polling_enabled); CHECK(overlay_count() == 0);
    } else {
        CHECK(!"unknown test");
    }
    printf("PASS %s\n", test_name);
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    test_name = argv[1];
    init();
    return run();
}
