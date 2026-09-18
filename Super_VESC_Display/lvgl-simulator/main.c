/*
 * SPDX-License-Identifier: MIT
 * Copyright 2025 NXP
 */

/*********************
 *      INCLUDES
 *********************/
#define _DEFAULT_SOURCE /* needed for usleep() */
#include <stdlib.h>
#include <unistd.h>
#define SDL_MAIN_HANDLED        /*To fix SDL's "undefined reference to WinMain" issue*/
#include <SDL2/SDL.h>
#include "lvgl/lvgl.h"
#include "lv_drivers/display/monitor.h"
#include "lv_drivers/indev/mouse.h"
#include "lv_drivers/indev/mousewheel.h"
#include "lv_drivers/indev/keyboard.h"
#include "gui_guider.h"
#include "custom.h"
#include "dashboard_theme.h"
#include "widgets_init.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static lv_obj_t *gear_preview_status;
static uint8_t gear_preview_value = DASH_MODE_PARK;
static const char *const gear_case_names[] = {"P", "1", "2", "3", "R", "Wait", "Stale"};
static const uint8_t gear_case_values[] = {DASH_MODE_PARK, 0, 1, 2,
    DASH_MODE_REVERSE, DASH_MODE_UNKNOWN, DASH_MODE_UNKNOWN};

static void gear_preview_tick(lv_timer_t *timer)
{
    (void)timer;
    dashboard_demo_set_active(false);
    update_mode_text(gear_preview_value);
}

static void gear_preview_select(lv_event_t *event)
{
    unsigned idx = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    gear_preview_value = gear_case_values[idx];
    const char *detail = idx == 5 ? "Interlock: reverse NOT ready" :
                         idx == 6 ? "Telemetry expired: permission unknown" :
                         "Injected display state (not a motor command)";
    lv_label_set_text(gear_preview_status, detail);
    update_mode_text(gear_preview_value);
}

static void gear_preview_open(void)
{
    dashboard_demo_set_active(false);
    update_speed(0);
    lv_obj_t *panel = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(panel, 195, 38);
    lv_obj_set_size(panel, 410, 112);
    lv_obj_set_style_pad_all(panel, 5, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "SIMULATION ONLY - Gear display cases");
    lv_obj_set_pos(title, 0, 0);
    for (unsigned i = 0; i < sizeof(gear_case_values); ++i) {
        lv_obj_t *button = lv_btn_create(panel);
        lv_obj_set_pos(button, i * 56, 26);
        lv_obj_set_size(button, 52, 34);
        lv_obj_add_event_cb(button, gear_preview_select, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, gear_case_names[i]);
        lv_obj_center(label);
    }
    gear_preview_status = lv_label_create(panel);
    lv_obj_set_pos(gear_preview_status, 0, 70);
    lv_label_set_text(gear_preview_status, "Boot preview: P (simulated)");
    update_mode_text(gear_preview_value);
    lv_timer_create(gear_preview_tick, 100, NULL);
}

static lv_obj_t *find_gear_circle(lv_obj_t *root)
{
    if (lv_obj_check_type(root, &lv_label_class) &&
        lv_obj_get_width(root) == DASH_GEAR_DIAMETER &&
        lv_obj_get_height(root) == DASH_GEAR_DIAMETER &&
        lv_obj_get_style_radius(root, LV_PART_MAIN) == LV_RADIUS_CIRCLE)
        return root;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        lv_obj_t *found = find_gear_circle(lv_obj_get_child(root, i));
        if (found) return found;
    }
    return NULL;
}

static void gear_render_self_test(void)
{
    static const char *const expected[] = {"P", "1", "2", "3", "R", "-", "-"};
    for (int theme = 0; theme < dashboard_theme_count(); ++theme) {
    dashboard_theme_set(theme);
    lv_obj_update_layout(dashboard_theme_active_screen());
    lv_obj_t *gear = find_gear_circle(dashboard_theme_active_screen());
    assert(gear);
    assert(lv_obj_get_style_text_font(gear, LV_PART_MAIN) == &lv_font_montserrat_30);
    assert(lv_font_montserrat_30.line_height + 4 <= DASH_GEAR_DIAMETER);
    for (unsigned i = 0; i < sizeof(gear_case_values); ++i) {
        update_mode_text(gear_case_values[i]);
        assert(strcmp(lv_label_get_text(gear), expected[i]) == 0);
        assert(lv_obj_get_style_text_color(gear, LV_PART_MAIN).full ==
            lv_color_hex(dashboard_gear_color(gear_case_values[i])).full);
        assert(lv_obj_get_style_border_color(gear, LV_PART_MAIN).full ==
            lv_color_hex(dashboard_gear_color(gear_case_values[i])).full);
    }
    update_mode_text(42); /* malformed value must never appear as gear 43 */
    assert(strcmp(lv_label_get_text(gear), "-") == 0);
    hide_mode_text();
    update_mode_text(DASH_MODE_REVERSE);
    assert(!lv_obj_has_flag(gear, LV_OBJ_FLAG_HIDDEN));
    assert(strcmp(lv_label_get_text(gear), "R") == 0);
    printf("PASS: theme %d, 9 gear cases, circle geometry and hide/re-show\n", theme);
    }
}

#if LV_USE_FREEMASTER
#include "external_data_init.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void hal_init(void);
static int tick_thread(void * data);

/**********************
 *  STATIC VARIABLES
 **********************/
volatile int keep_running = 0;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

lv_ui guider_ui;

#if LV_USE_FREEMASTER
pthread_t thread[2];
pthread_mutex_t jsonrpc_mutex;
pthread_mutex_t lvgl_mutex;
pthread_mutex_t gg_edata_ll_mutex;
pthread_cond_t gg_edata_ll_cond;
#endif

int main(int argc, char ** argv)
{
#if LV_USE_FREEMASTER
    if(argc > 1)
    {
        int sim_conf(int argc, char *argv[]);
        sim_conf(argc, argv);
    }
#endif

    /* local threads will run */
    keep_running = 1;

    /*Initialize LittlevGL*/
    lv_init();

    /*Initialize the HAL (display, input devices, tick) for LittlevGL*/
    hal_init();

#if LV_USE_FREEMASTER
    pthread_mutex_init(&gg_edata_ll_mutex, NULL);
    pthread_cond_init(&gg_edata_ll_cond, NULL);
    /*Initialize the external data */
    external_task_init(&guider_ui);
#endif

    /*Create a GUI-Guider app */
    setup_ui(&guider_ui);
    custom_init(&guider_ui);
    /* Direct entry for BMS frontend work and screenshot automation. */
    if(argc > 1 && strcmp(argv[1], "--bms-preview") == 0) {
        show_bms_viewer();
    }
    if(argc > 1 && strcmp(argv[1], "--gear-preview") == 0) {
        gear_preview_open();
    }
    if(argc > 1 && strcmp(argv[1], "--gear-self-test") == 0) {
        dashboard_demo_set_active(false);
        gear_render_self_test();
        keep_running = 0;
    }
#if LV_USE_FREEMASTER
    pthread_mutex_init(&jsonrpc_mutex, NULL);
    pthread_mutex_init(&lvgl_mutex, NULL);
    memset(&thread, 0, sizeof(thread));
    /*Create a separate thread to loop the linked list*/
    pthread_create(&thread[0], NULL, gg_edata_task_exec, &jsonrpc_mutex);
#endif

    while(keep_running) {
#if LV_USE_FREEMASTER
        pthread_mutex_lock(&lvgl_mutex);
        process_cached_widget_data();
#endif
        /* Periodically call the lv_task handler.
         * It could be done in a timer interrupt or an OS task too.*/
        lv_task_handler();
#if LV_USE_VIDEO
        video_play(&guider_ui);
#endif
#if LV_USE_FREEMASTER
        pthread_mutex_unlock(&lvgl_mutex);
#endif
        usleep(5 * 1000);
    }

#if LV_USE_FREEMASTER
    /* wakeup gg_edata_task_exec thread if it sleeps, let it finish */
    pthread_cond_signal(&gg_edata_ll_cond);
    /* wait for the gg_edata_task_exec thread*/
    pthread_join(thread[0], NULL);

    freemaster_disconnect();
#endif

    SDL_Quit();
    return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Initialize the Hardware Abstraction Layer (HAL) for the Littlev graphics library
 */
static void hal_init(void)
{
    /* Create default group */
    lv_group_set_default(lv_group_create());

    /* Use the 'monitor' driver which creates window on PC's monitor to simulate a display*/
    monitor_init();

    /*Create a display buffer*/
    static lv_disp_draw_buf_t disp_buf1;
    static lv_color_t buf1_1[MONITOR_HOR_RES * MONITOR_VER_RES];
    lv_disp_draw_buf_init(&disp_buf1, buf1_1, NULL, MONITOR_HOR_RES * MONITOR_VER_RES);

    /*Create a display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);            /*Basic initialization*/
    disp_drv.draw_buf = &disp_buf1;
    disp_drv.flush_cb = monitor_flush;
    disp_drv.hor_res = MONITOR_HOR_RES;
    disp_drv.ver_res = MONITOR_VER_RES;
    lv_disp_drv_register(&disp_drv);

    /* Add the mouse as input device
     * Use the 'mouse' driver which reads the PC's mouse*/
    mouse_init();
    static lv_indev_drv_t mouse_indev_drv;
    lv_indev_drv_init(&mouse_indev_drv);          /*Basic initialization*/
    mouse_indev_drv.type = LV_INDEV_TYPE_POINTER;
    mouse_indev_drv.read_cb = mouse_read;         /*This function will be called periodically (by the library) to get the mouse position and state*/
    lv_indev_drv_register(&mouse_indev_drv);

#if USE_KEYBOARD
    /* Create default group */
    lv_group_set_default(lv_group_create());

    /* register keyboard device */
    static lv_indev_drv_t keyboard_indev_drv;
    lv_indev_drv_init(&keyboard_indev_drv);       /*Basic initialization*/
    keyboard_indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    keyboard_indev_drv.read_cb = keyboard_read;
    lv_indev_t *kb_indev = lv_indev_drv_register(&keyboard_indev_drv);
    lv_indev_set_group(kb_indev, lv_group_get_default());
#endif

    /* Tick init.
     * You have to call 'lv_tick_inc()' in periodically to inform LittelvGL about how much time were elapsed
     * Create an SDL thread to do this*/
    SDL_CreateThread(tick_thread, "tick", NULL);
}

/**
 * A task to measure the elapsed time for LittlevGL
 * @param data unused
 * @return never return
 */
static int tick_thread(void * data)
{
    (void)data;

    while(keep_running) {
        SDL_Delay(5);   /*Sleep for 5 millisecond*/
        lv_tick_inc(5); /*Tell LittelvGL that 5 milliseconds were elapsed*/
    }

    return 0;
}

#if LV_USE_FREEMASTER
/**
 * Enable simulator to be linked and used as DLL. This function is invoked by 
 * FreeMASTER from its background thread. This function must finish when 
 * sim_exit() is called asynchronously.
 */
__declspec(dllexport)
int sim_main(int argc, char *argv[])
{
    int ret = main(argc, argv);
    return ret;
}

/**
 * Finish all running threads and terminate the application running as sim_main().
 */
__declspec(dllexport)
int sim_exit(void)
{
    keep_running = 0;
    return 0;
}

/**
 * Enable FreeMASTER to configure the simulator before starting it.
 * This function is the first one called by the FreeMASTER from its GUI thread. 
 */
__declspec(dllexport)
int sim_conf(int argc, char *argv[])
{
    /* Process the arguments */
    if(argc > 0 && argv != NULL)
    {
        for(int i=0; i<argc; i++)
        {
            /* --rpcs is used to specify FreeMASTER JSON-RPC server address. */
            if(strcmp(argv[i], "--rpcs") == 0)
            {
                i++;
                if(i<argc && strncmp(argv[i], "ws://", 5) == 0)
                {
                    freemaster_server = strdup(argv[i]);
                }
            }
        }
    }
    return 0;
}

/**
 * Enable host application to retrieve DLL information
 */
__declspec(dllexport)
const void * sim_info(void)
{
    #define SIM_F_RESIZE_UP        0x0001   /* Enable growing size within parent view */
    #define SIM_F_RESIZE_DOWN      0x0002   /* Enable shrinking size within parent view */
    #define SIM_F_KEEP_ASPECT      0x0004   /* When RESIZE used, preserve aspect ratio */
    #define SIM_F_CENTER_H         0x0008   /* Center horizontally */
    #define SIM_F_CENTER_V         0x0010   /* Center vertically */
    #define SIM_F_CENTER_FORCE     0x0020   /* Keep centered even if parent is smaller (move beyond top/left border) */
    #define SIM_F_AUTO_BACKCOLOR   0x0040   /* Determine background color automatically */

    typedef struct
    {
        uint32_t sz;
        const char* name;
        const char* desc;
        const char* copyright;
        const char* server;
        uint32_t width;
        uint32_t height;
        uint32_t flags;
        uint32_t background;
        uint32_t reserved;
    } sim_info_t;

    /* Constant options: */
    static sim_info_t info =
    {
        .sz = sizeof(sim_info_t),
        .name = "NXP GUI-Guider Project",
        .desc = "Designed to enable GUI designs created in GG to be hosted in FreeMASTER",
        .copyright = "Copyright 2024 NXP",
        .server = "",
        .width = MONITOR_HOR_RES,
        .height = MONITOR_VER_RES,
        .flags = SIM_F_CENTER_H,
        .background = 0,
    };

    /* Runtime-defined options: */
    info.server = freemaster_server;

    return &info;
}

#endif
