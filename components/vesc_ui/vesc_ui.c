#include "vesc_ui.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "gui_guider.h"
#include "events_init.h"
#include "custom.h"
#include "dashboard_theme.h"
#include "dev_settings.h"

/* Single global GUI Guider state — referenced as `extern lv_ui guider_ui`
 * by generated and custom code. Definition lives here. */
lv_ui guider_ui;

static bool s_inited;

void vesc_ui_init(void)
{
    if (s_inited) return;
    int64_t t0 = esp_timer_get_time();
    /* Like setup_ui() in gui_guider.c, but theme-aware and minus the
     * lv_scr_load() — ui_mode controls which screen is active. custom_init_once()
     * registers the themes (and inits NVS settings); dashboard_theme_build()
     * then constructs the saved theme's screen offscreen into guider_ui.dashboard. */
    init_scr_del_flag(&guider_ui);
    init_keyboard(&guider_ui);
    custom_init_once();
    int64_t t1 = esp_timer_get_time();
    dashboard_theme_build(settings_get_dashboard_theme());
    int64_t t2 = esp_timer_get_time();
    ESP_LOGI("vesc_ui", "init: once=%lldms build=%lldms TOTAL=%lldms (theme=%d/%d)",
             (t1 - t0) / 1000, (t2 - t1) / 1000, (t2 - t0) / 1000,
             dashboard_theme_active_index(), dashboard_theme_count());
    s_inited = true;
}

lv_obj_t *vesc_ui_get_screen(void)
{
    return s_inited ? dashboard_theme_active_screen() : NULL;
}
