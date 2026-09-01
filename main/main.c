#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_lv_adapter_input.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

/* Constructor that runs LATE in do_global_ctors() (priority 65535 = lowest
 * among priority-tagged, but still before untagged constructors).
 * Prints free MALLOC_CAP_INTERNAL+8BIT heap to bracket who consumed it. */
__attribute__((constructor(65535)))
static void heap_probe_post_priority_ctors(void)
{
    size_t internal8 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    esp_rom_printf("HEAP_PROBE A (post-priority-ctors) free=%u largest=%u\n",
                   (unsigned)internal8, (unsigned)largest);
}

/* port_start_app_hook is a weak symbol declared in
 * components/freertos/app_startup.c, called from esp_startup_start_app()
 * AFTER xTaskCreatePinnedToCore(main_task) but BEFORE vTaskStartScheduler.
 * That's the last point where we can measure heap before IDLE allocs.
 *
 * Keep an eye on both probes on ESP32-S3: internal SRAM is 512 KB there
 * against the P4's 768 KB, and Wi-Fi + the BLE controller both want
 * DMA-capable internal blocks. */
void port_start_app_hook(void)
{
    size_t internal8 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    esp_rom_printf("HEAP_PROBE B (pre-scheduler) free=%u largest=%u\n",
                   (unsigned)internal8, (unsigned)largest);
}

#include "ble_host.h"
#include "ble_nus.h"
#include "notif_bridge.h"
#include "notif_toast.h"
#include "pas.h"
#include "gui_guider.h"
#include "dashboard_theme.h"
#include "config.h"
#include "dev_settings.h"
#include "display_init.h"
#include "idle_screen.h"
#include "log_capture.h"
#include "ota_http.h"
#include "files_http.h"
#include "lisp_http.h"
#include "ota_screen.h"
#include "touch_input.h"
#include "vesc_can/comm_can.h"
#include "vesc_battery_calc.h"
#include "vesc_can/vesc_lisp_poll.h"
#include "vesc_can/vesc_lisp_console.h"
#include "vesc_can/vesc_rt_data.h"
#include "vesc_can/vesc_io_data.h"
#include "vesc_can/vesc_lisp_code.h"
#include "vesc_can/vesc_lisp_panel.h"
#include "vesc_can/vesc_ride_mode.h"
#include "vesc_config/vesc_config.h"
#include "vesc_config/vesc_config_transport.h"
#include "vesc_sim.h"
#include "vesc_trip_persist.h"
#include "vesc_ui.h"
#include "trip_log.h"
#include "vesc_ui_updater.h"
#include "wifi_manager.h"

static const char *TAG = "main";

/* Force ld to pull main/files_screen.c.o out of libmain.a so its strong
 * files_screen_show() overrides the weak printf stub in
 * components/vesc_ui/custom.c (kept so vesc_ui links standalone and the
 * desktop simulator still builds). libvesc_ui.a comes earlier on the link
 * line, so its weak definition would otherwise satisfy custom.c's call and
 * files_screen.c.o would never be pulled in. files_screen.c exports a unique
 * non-weak anchor symbol; referencing it by address here makes ld follow the
 * strong undefined ref into libmain.a, dragging the strong files_screen_show
 * along to win the weak-vs-strong tie-break. */
extern const int files_screen_link_anchor;
__attribute__((used))
static const int *const force_link_main_strongs[] = {
    &files_screen_link_anchor,
};

/* Defined in Super_VESC_Display/custom/lisp_panel.c (vesc_ui component). Opens
 * the LISP quick-action panel; safe to call from any task — it marshals onto
 * the LVGL thread and no-ops unless the dashboard is the live screen. Declared
 * here to avoid pulling the heavy gui_guider/custom.h into the firmware side. */
extern void lisp_panel_open_async(void);

/* ---- Custom LVGL touch indev fed by touch_input.c ----
 *
 * The BSP auto-installs its own LVGL touch indev that reads the controller
 * directly. We unregister it and create our own pointer-typed indev that
 * pulls from touch_input's shared state, so there is exactly ONE reader on
 * the touch I2C bus. (On the P4 build this also gated touch away from LVGL
 * while Android Auto owned the screen; with AA gone the indev is simply the
 * single reader.)
 *
 * LVGL 8.3 indev API: lv_indev_drv_init + lv_indev_drv_register
 * (lv_indev_create / lv_indev_set_* arrived in LVGL 9). */
static lv_indev_drv_t s_lvgl_touch_drv;

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    bool pressed = false;
    uint16_t x = 0, y = 0;
    touch_input_lvgl_read(&x, &y, &pressed);
    data->state   = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = x;
    data->point.y = y;
}

static void install_lvgl_touch_indev(void)
{
    /* Drop BSP's auto-installed touch indev (it would race our reader on
     * the same touch controller over I2C). */
    lv_indev_t *bsp_indev = bsp_display_get_input_dev();
    if (bsp_indev) {
        if (esp_lv_adapter_unregister_touch(bsp_indev) == ESP_OK) {
            ESP_LOGI(TAG, "BSP touch indev unregistered");
        } else {
            ESP_LOGW(TAG, "BSP touch indev unregister failed");
        }
    }

    /* lv_indev_drv_register touches LVGL globals — hold the BSP lock. */
    if (bsp_display_lock(1000) != ESP_OK) {
        ESP_LOGW(TAG, "lvgl indev: bsp_display_lock failed — skipping");
        return;
    }
    lv_indev_drv_init(&s_lvgl_touch_drv);
    s_lvgl_touch_drv.type    = LV_INDEV_TYPE_POINTER;
    s_lvgl_touch_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_t *indev = lv_indev_drv_register(&s_lvgl_touch_drv);
    bsp_display_unlock();
    if (indev) {
        ESP_LOGI(TAG, "custom LVGL touch indev registered");
    } else {
        ESP_LOGW(TAG, "custom LVGL touch indev registration failed");
    }
}

/* Build the GUI-Guider dashboard and make it the live screen. This replaces
 * the old main/ui_mode.c, which existed only to flip between the dashboard
 * and the Android Auto projection screen. The dashboard is now the only
 * screen, so there is no mode to switch. */
static esp_err_t dashboard_init(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl lock timeout building dashboard");
        return ESP_FAIL;
    }
    vesc_ui_init();
    lv_obj_t *scr = vesc_ui_get_screen();
    if (scr) lv_scr_load(scr);
    bsp_display_unlock();
    return ESP_OK;
}

/* Re-assembled VESC packets land here. Forwards to the RT-data parser,
 * (if enabled) the LISP poll parser, and the BLE NUS bridge so VESC Tool
 * over BLE sees CAN responses. All three filter / gate on their own state
 * (RT/LISP on the leading COMM_PACKET_ID byte, NUS on connection state)
 * so the fan-out is unconditional. */
static void vesc_packet_dispatch(const uint8_t *data, unsigned int len)
{
    vesc_rt_data_process_response(data, len);
    /* COMM_LISP_GET_STATS replies. Unconditional: the periodic poll is off by
     * default, but the web editor asks for stats one request at a time and
     * still needs the answer parsed. Gates on data[0] like everything here. */
    vesc_lisp_poll_process_response(data, len);
    /* Script output — COMM_LISP_PRINT / COMM_PRINT — into the console ring
     * the web editor polls. Unsolicited, arrives whenever a script prints. */
    vesc_lisp_console_process_response(data, len);
    /* ADC/PPM decoded inputs for the realtime viewer (gated active). */
    vesc_io_data_process_response(data, len);
    /* LISP code upload/read acks (gated on an in-flight operation). */
    vesc_lisp_code_process_response(data, len);
    /* Config GET/SET/FW_VERSION replies (COMM ids 0, 13-18). Gates on data[0]
     * and ignores packets it doesn't own. */
    vesc_config_transport_process_response(data, len);
    /* LISP quick-action panel UI_DESC/STATE replies (COMM_CUSTOM_APP_DATA +
     * 'VP' magic). Gates internally; ignores everything else. */
    vesc_lisp_panel_process_response(data, len);
    /* Ride-mode config/status replies. Same COMM_CUSTOM_APP_DATA + 'VP'
     * channel as the panel, different message ids (0x87/0x89); each gates on
     * its own and ignores the other's traffic. */
    vesc_ride_mode_process_response(data, len);
    ble_nus_forward_response(data, (uint16_t)len);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* settings_set_can_speed → here. Re-arms the TWAI driver with the new
 * baud rate, keeping the current controller_id from settings. */
static void on_can_speed_changed(int new_kbps)
{
    if (comm_can_reinit(settings_get_controller_id(), new_kbps) != ESP_OK) {
        ESP_LOGW(TAG, "comm_can_reinit(%d kbps) failed", new_kbps);
    }
}

/* settings_set_screen_brightness → here. bsp_display_brightness_set is
 * a thin LEDC duty wrapper, safe to call from the UI task. */
static void on_brightness_changed(uint8_t pct)
{
    bsp_display_brightness_set(pct);
}

/* dashboard_theme switch hook → re-home the phone-side music tile onto the
 * active theme's widget (or tear it down when the theme has none). Fires on the
 * first build and on every live theme switch, always on the LVGL thread with
 * the BSP lock already held by the caller.
 *
 * TODO(port): the body is stubbed out because main/music_info_view.c is not
 * compiled yet (P4 hardware JPEG decoder for the album art) — see
 * main/CMakeLists.txt. Restore the two calls once that file builds. */
static void on_dashboard_theme_switched(lv_obj_t *screen, lv_obj_t *music_tile)
{
    (void)screen;
    (void)music_tile;
    /* music_info_view_detach();
     * if (music_tile) music_info_view_attach(music_tile); */
}

/* settings_set_controller_id → here. Reinit TWAI so STATUS_* frames go
 * out under the new ID. Speed comes back from settings (single source). */
static void on_controller_id_changed(uint8_t new_id)
{
    if (comm_can_reinit(new_id, (int)settings_get_can_speed()) != ESP_OK) {
        ESP_LOGW(TAG, "comm_can_reinit(ctrl=%u) failed", new_id);
    }
}

/* settings_set_target_vesc_id → here. Both pollers store the target ID
 * statically; vesc_*_init is documented as safe to call repeatedly so
 * we just re-init them and the next poll cycle hits the new node. */
static void on_target_id_changed(uint8_t new_id)
{
    vesc_rt_data_init(new_id, CONFIG_VESC_CAN_RT_INTERVAL_MS);
    vesc_lisp_poll_init(new_id, CONFIG_VESC_CAN_LISP_INTERVAL_MS);
    vesc_io_data_init(new_id, 150);
    vesc_lisp_code_set_target(new_id);
    vesc_lisp_panel_set_target(new_id);
    ESP_LOGI(TAG, "VESC target ID → %u", new_id);
}

void app_main(void)
{
    /* Install the PSRAM-backed log ring buffer before anything else
     * logs, so the Logs screen in Settings can show the full boot
     * sequence (NVS contents, BLE init, …). */
    log_capture_init();

    ESP_LOGI(TAG, "VESC display boot");

    /* Why did we (re)start? A mid-ride restart is invisible on the dashboard
     * but resets every in-RAM total, so the Logs screen has to be able to
     * answer it after the fact: POWERON is the normal case, TASK_WDT / INT_WDT
     * / PANIC point at a firmware hang, BROWNOUT at the supply. Logged at WARN
     * for anything other than a clean power-on so it stands out in the ring. */
    {
        esp_reset_reason_t rr = esp_reset_reason();
        const char *name;
        switch (rr) {
        case ESP_RST_POWERON:  name = "POWERON";  break;
        case ESP_RST_SW:       name = "SW";       break;
        case ESP_RST_PANIC:    name = "PANIC";    break;
        case ESP_RST_INT_WDT:  name = "INT_WDT";  break;
        case ESP_RST_TASK_WDT: name = "TASK_WDT"; break;
        case ESP_RST_WDT:      name = "WDT";      break;
        case ESP_RST_BROWNOUT: name = "BROWNOUT"; break;
        case ESP_RST_EXT:      name = "EXT";      break;
        case ESP_RST_DEEPSLEEP:name = "DEEPSLEEP";break;
        default:               name = "OTHER";    break;
        }
        if (rr == ESP_RST_POWERON || rr == ESP_RST_SW) {
            ESP_LOGI(TAG, "reset reason: %s (%d)", name, (int)rr);
        } else {
            ESP_LOGW(TAG, "reset reason: %s (%d) — previous run did not exit cleanly",
                     name, (int)rr);
        }
    }

    /* Publish our own firmware version to dev_settings so the Settings screen
     * can render it. The P4 build also reported the C6 and BT-agent firmware
     * versions here; neither co-processor exists on ESP32-S3 / ESP32. */
    {
        const esp_app_desc_t *desc = esp_app_get_description();
        if (desc) fw_info_set_p4(desc->version);
    }
    ESP_LOGI(TAG, "HEAP_PROBE: app_main INTERNAL+8BIT free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    init_nvs();
    /* Settings cache is now ready for both the UI (settings_ui_init pulls
     * via settings_wrapper_init → settings_init) and our callback wiring
     * below. settings_init is idempotent. */
    settings_init();

    /* Smart battery calc needs NVS open before its first percentage call.
     * settings_init's nvs_flash_init has already run; battery_calc keeps its
     * own namespace ("battery_calc") for the remaining-Ah counter. */
    battery_calc_init();
    /* trip_persist tracks trip / Ah / uptime across VESC reboots — same NVS
     * pattern as battery_calc, separate namespace. The dashboard reset icon
     * eventually flows here via battery_calc_reset_trip_and_ah(). */
    trip_persist_init();
    /* Per-trip history to LittleFS (/vescfs/trips/<id>/). Mounts the backup FS
     * at boot and rolls over to a new trip on reset / battery swap. */
    trip_log_init();

    if (display_init() != ESP_OK) {
        ESP_LOGW(TAG, "display init failed — UI disabled");
    }
    /* display_init lights the backlight at 100% — apply the saved brightness
     * once the panel is up so user preference takes hold before any UI
     * draws. Brightness callback persists future changes from the slider. */
    bsp_display_brightness_set(settings_get_screen_brightness());
    settings_register_brightness_cb(on_brightness_changed);

    /* TODO(port): boot splash GIF. main/splash_screen.c is not compiled yet —
     * it pre-rotates frames with the P4 PPA. See main/CMakeLists.txt. */

    /* idle first, ota second so the OTA overlay sits on top in z-order
     * (children of lv_scr_act() are stacked in creation order). */
    idle_screen_init();
    ota_screen_init();

    /* Replace BSP's auto-installed LVGL touch indev with our own that reads
     * from touch_input's shared state — single reader on the touch bus. Must
     * run before dashboard_init so the dashboard sees touch from frame 1. */
    install_lvgl_touch_indev();

    /* Register the music-tile re-home hook BEFORE the dashboard is built, so
     * the very first theme build (inside dashboard_init) attaches the music
     * tile via the same path a live theme switch uses. */
    dashboard_theme_set_switch_cb(on_dashboard_theme_switched);

    /* Build the VESC dashboard.
     *
     * vesc_ui_init() walks ~1700 lines of GUI Guider widget creation
     * (~750 lv_obj_set_style_* calls) under the BSP lock on prio-1 main task
     * → IDLE0 starves for ~5 s on the P4 and CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
     * fires. Expect this to be SLOWER on a 240 MHz ESP32-S3. Detach IDLE0
     * from TWDT just for this one-shot init. */
    TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCore(0);
    bool wdt_paused = (idle0 && esp_task_wdt_delete(idle0) == ESP_OK);
    esp_err_t ui_err = dashboard_init();
    if (wdt_paused) {
        esp_task_wdt_add(idle0);
    }

    if (ui_err == ESP_OK) {
        /* Left-edge swipe opens the LISP quick-action panel. The handler
         * marshals to the LVGL task and no-ops unless the dashboard is the
         * live screen, so registering it unconditionally is safe. */
        touch_input_set_edge_swipe_cb(lisp_panel_open_async);
        touch_input_start(NULL, NULL);
    }

    /* TODO(port): debug bridge over the UART0 console (host-driven screenshot
     * + touch injection). main/debug_uart_bridge.c is not compiled yet — it
     * uses the P4 hardware JPEG encoder. See main/CMakeLists.txt. */

    /* VESC CAN bring-up — runs the second the dashboard is alive so RT data
     * starts streaming even before Wi-Fi is up. The decode-side handler routes
     * reassembled VESC packets to vesc_rt_data (and vesc_lisp_poll if
     * enabled). */
    int     can_kbps = (int)settings_get_can_speed();
    uint8_t ctrl_id  = settings_get_controller_id();
    uint8_t tgt_id   = settings_get_target_vesc_id();

    if (settings_get_vesc_emulator()) {
        /* Synthetic source — runs a scripted drive cycle and injects into
         * vesc_rt_data. No CAN driver, no real polling. Useful on the bench
         * before the CAN transceiver is wired up. */
        vesc_rt_data_init(tgt_id, CONFIG_VESC_CAN_RT_INTERVAL_MS);
        vesc_sim_start();
        ESP_LOGW(TAG, "VESC EMULATOR active — no real CAN");
        /* Config menu backed by in-RAM defaults (no CAN in emulator mode). */
        vesc_config_init();
        vesc_ui_updater_start();
    } else if (comm_can_start(CONFIG_VESC_CAN_TX_GPIO, CONFIG_VESC_CAN_RX_GPIO,
                              ctrl_id, can_kbps) == ESP_OK) {
        /* Identity for VESC Tool's CAN scan: it pings the bus, then asks each
         * node that answered for its firmware version, and lists whoever stays
         * quiet as "Unknown". UUID = our WiFi MAC so two units on one bus are
         * still distinguishable. */
        {
            uint8_t mac[6] = {0};
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            const esp_app_desc_t *desc = esp_app_get_description();
            unsigned maj = 0, min = 0;
            if (desc) sscanf(desc->version, "%u.%u", &maj, &min);
            comm_can_set_fw_info("Super VESC Display", (uint8_t)maj, (uint8_t)min,
                                 mac, sizeof(mac));
        }
        vesc_rt_data_init(tgt_id, CONFIG_VESC_CAN_RT_INTERVAL_MS);
        /* Unconditional: init only stores the target id and leaves the poll
         * inactive. The periodic poll still needs its Kconfig (below), but the
         * web editor's one-shot stats request needs the right target either way. */
        vesc_lisp_poll_init(tgt_id, CONFIG_VESC_CAN_LISP_INTERVAL_MS);
        vesc_lisp_console_init();
        /* ADC/PPM poller: inactive until the realtime viewer opens it. */
        vesc_io_data_init(tgt_id, 150);
        /* LISP code upload/read worker (used by the LISP editor screen). */
        vesc_lisp_code_init(tgt_id);
        /* LISP quick-action panel (swipe-out drawer driven by the master
         * LISP script). Reply CAN id is fetched live from comm_can. */
        vesc_lisp_panel_init(tgt_id);
        /* Ride-mode/reverse backend. Lisp owns the config and the
         * interlocks; this only asks, submits and reports. */
        vesc_ride_mode_init(tgt_id);
        comm_can_set_packet_handler(vesc_packet_dispatch);
        vesc_rt_data_start();
        vesc_rt_data_start_task();
        /* Probe the downstream VESC's firmware version over CAN and pick the
         * matching config table. Reply routed via vesc_packet_dispatch. */
        vesc_config_init();
        vesc_config_probe_fw();
#if CONFIG_VESC_CAN_LISP_POLL_ENABLE
        /* start() only flips the active flag. The pumping happens inside the
         * single CAN-polling task spawned by vesc_rt_data_start_task() above —
         * see rt_task() in vesc_rt_data.c. */
        vesc_lisp_poll_start();
#endif
        ESP_LOGI(TAG, "VESC CAN ready, polling target ID %u (own ID %u)",
                 tgt_id, ctrl_id);
        vesc_ui_updater_start();
        /* Hook the setters that drive the CAN bus to live reconfig.
         * Registered after the driver is up so callbacks can never run
         * before the first comm_can_start. */
        settings_register_can_speed_cb(on_can_speed_changed);
        settings_register_controller_id_cb(on_controller_id_changed);
        settings_register_target_id_cb(on_target_id_changed);
    } else {
        ESP_LOGW(TAG, "VESC CAN init failed — dashboard will show no data");
    }

    /* NimBLE on the native controller. Starts advertising NUS so VESC Tool
     * can connect over BLE and talk to the VESC controller via the CAN bridge
     * in vesc_packet_dispatch.
     *
     * ble_nus_init brings up the outbound ring buffer + TX task BEFORE
     * NimBLE so the first reply that lands during VESC Tool's handshake
     * already has somewhere to queue without back-pressuring the CAN task. */
    ble_nus_init();
    if (ble_host_init() != ESP_OK) {
        ESP_LOGW(TAG, "BLE host init failed — VESC Tool over BLE unavailable");
    }
    /* Phone-side notifications/media overlay. ble_host_init already calls
     * notif_bridge_init for the GATT plumbing; this one wires the LVGL
     * toast widget on top so incoming notifications actually appear. */
    notif_toast_init();

    /* Pedal-assist: load settings + bound cadence sensor from NVS and start the
     * control task. Safe to call regardless of CAN state — it only forwards a
     * current setpoint to the LISP arbiter when the CAN poll task is running. */
    pas_init();

    /* Wi-Fi last: everything above works without it. Not ESP_ERROR_CHECK —
     * a Wi-Fi failure must not take the dashboard down with it, and returning
     * from app_main leaves every other task running. */
    if (wifi_manager_start() != ESP_OK ||
        wifi_manager_wait_ready(30000) != ESP_OK) {
        ESP_LOGE(TAG, "wifi setup failed — web UI and HTTP OTA unavailable");
        return;
    }

    const wifi_ap_info_t *ap = wifi_manager_get_ap_info();
    if (ap) {
        ESP_LOGI(TAG, "AP \"%s\" psk \"%s\" bssid %s ch %u",
                 ap->ssid, ap->password, ap->bssid_str, (unsigned)ap->channel);
    }

    /* Plain HTTP OTA server. No-op when CONFIG_OTA_HTTP_ENABLED is unset. */
    ota_http_start();
    /* Attach the web file manager (/files) to the OTA HTTP server — browse
     * /vescfs + microSD from any browser on the AP. No-op if the server
     * didn't start. */
    files_http_register(ota_http_get_server());
    /* Web LISP editor (/lisp) on the same server — edit + upload the VESC's
     * LispBM script from a browser instead of the touch keyboard. */
    lisp_http_register(ota_http_get_server());

    /* Compose a one-line status with our IP for the idle screen.
     * AP mode shows the SSID; STA mode shows the joined network IP. */
    char status_line[80];
    esp_netif_ip_info_t ip_info = {0};
    if (ap) {
        esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (n) esp_netif_get_ip_info(n, &ip_info);
        snprintf(status_line, sizeof(status_line),
                 "AP %s | %d.%d.%d.%d", ap->ssid, IP2STR(&ip_info.ip));
    } else {
        esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (n) esp_netif_get_ip_info(n, &ip_info);
        snprintf(status_line, sizeof(status_line),
                 "%d.%d.%d.%d", IP2STR(&ip_info.ip));
    }
    ESP_LOGI(TAG, "web UI ready: %s", status_line);
}
