/* Production decoders/head2 linked whole. UI pump functions extracted verbatim.
 * This harness records setter arguments; it does not pretend to render LVGL. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "vesc_can/comm_can.h"
#include "vesc_can/vesc_rt_data.h"
#include "vesc_can/vesc_io_data.h"
#include "vesc_head2.h"
#include "ride_gear_state.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); failures++; } } while(0)
#define CLOSE(a,b) CHECK(fabs((double)(a)-(double)(b)) < 0.0002)
static uint32_t now_ms=1000;
static bool secondary_enabled, secondary_present, demo;
static uint8_t secondary_id=11;
static int requested_id=-1, active_screen=1;
static can_status_msg_4 secondary;
static vesc_ride_safety_t safety;
static float displayed_motor, displayed_current, displayed_fet;
static unsigned motor_calls, trip_log_calls;
static bool connected;
static uint8_t displayed_gear;
typedef int lv_timer_t;
#define ENABLE_WALL_CLOCK 0
#define DASH_MODE_REVERSE 0xffu
#define DASH_MODE_PARK 0xfeu
#define DASH_MODE_UNKNOWN 0xfdu

int64_t esp_timer_get_time(void) { return (int64_t)now_ms*1000; }
TickType_t xTaskGetTickCount(void) { return now_ms; }
bool settings_get_second_head_enabled(void) { return secondary_enabled; }
uint8_t settings_get_second_head_id(void) { return secondary_id; }
can_status_msg_4 *comm_can_get_status_msg_4_id(int id) {
    requested_id=id;
    return secondary_present && id==secondary.id ? &secondary : NULL;
}
void vTaskDelay(TickType_t ticks) { (void)ticks; }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,uint32_t stack,
 void *arg,unsigned priority,TaskHandle_t *handle,int core) {
    (void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;(void)core;return pdPASS;
}
void comm_can_send_buffer_sync(uint8_t id,const uint8_t *data,unsigned len,uint8_t send,uint32_t timeout) {
    (void)id;(void)data;(void)len;(void)send;(void)timeout;
}
void vesc_lisp_poll_once_loop(void) {}
void vesc_lisp_panel_dash_loop(void) {}
void vesc_lisp_panel_poll_loop(void) {}
void vesc_ride_mode_poll_loop(void) {}
void vesc_lisp_panel_pas_loop(void) {}
bool vesc_ride_mode_get_safety(vesc_ride_safety_t *out) { *out=safety;return safety.valid; }
static void update_temp_motor(float value) { displayed_motor=value;motor_calls++; }
static void update_temp_fet(float value) { displayed_fet=value; }
static void update_current(float value) { displayed_current=value; }
static void update_esc_connection_status(bool value) { connected=value; }
static void update_mode_text(uint8_t value) { displayed_gear=value; }
#define NOOP_FLOAT(fn) static void fn(float value) { (void)value; }
NOOP_FLOAT(update_speed)
NOOP_FLOAT(update_battery_proc)
NOOP_FLOAT(update_trip)
NOOP_FLOAT(update_range)
NOOP_FLOAT(update_amp_hours)
NOOP_FLOAT(update_battery_temp)
NOOP_FLOAT(update_battery_voltage)
NOOP_FLOAT(update_odometer)
NOOP_FLOAT(battery_calc_voltage_boot_check)
static void update_fps(int value) { (void)value; }
static void update_uptime(uint32_t value) { (void)value; }
static void update_cruise_control_status(bool value) { (void)value; }
static void update_ble_status(bool value) { (void)value; }
static bool ble_host_is_connected(void) { return false; }
static bool dashboard_demo_is_active(void) { return demo; }
static int lv_scr_act(void) { return active_screen; }
static int dashboard_theme_active_screen(void) { return 1; }
static bool notif_bridge_get_phone_time(int *h,int *m) { (void)h;(void)m;return false; }
static void update_cur_time_hm(int h,int m) { (void)h;(void)m; }
static void hide_cur_time(void) {}
static void trip_persist_update(float t,float a,uint32_t u) { (void)t;(void)a;(void)u; }
static void trip_log_tick(const vesc_setup_values_t *rt) { (void)rt;trip_log_calls++; }
static float battery_calc_display_percentage(float level,float a,float b) { (void)a;(void)b;return level*100; }
static float trip_persist_get_trip_km(void) { return 0; }
static float trip_persist_get_amp_hours(void) { return 0; }
static uint32_t trip_persist_get_uptime_ms(void) { return 0; }
static float compute_range_km(void) { return 0; }
#include "updater_functions.inc"
#include "viewer_enum.inc"
static bool s_alive=true;
static bool viewer_valid[RT_COUNT];
static double viewer_value[RT_COUNT];
static void set_val(rt_field_t field,bool valid,const char *fmt,double value) {
    (void)fmt;viewer_valid[field]=valid;viewer_value[field]=value;
}
#include "viewer_functions.inc"

static void dispatch(const uint8_t *p,unsigned len) {
    vesc_rt_data_process_response(p,len);
    vesc_io_data_process_response(p,len);
}
static void put16(uint8_t *p,int value) { p[0]=(uint8_t)((uint16_t)value>>8);p[1]=(uint8_t)value; }
static void put32(uint8_t *p,uint32_t value) {
    p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);
    p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value;
}
static void temperature(int decic) {
    uint8_t p[]={51,0,0,0,3,1,44,0,0};
    put16(p+7,decic);dispatch(p,sizeof p);
}
static void tick(void) { updater_lv_timer_cb(NULL); }

static void video_values_adc_current_gear_independent(void) {
    const int values[]={-999,20,10,-170};
    for (unsigned v=0;v<4;v++) {
        temperature(values[v]);tick();
        CLOSE(displayed_motor,values[v]/10.0f);CLOSE(displayed_fet,30);
        for (unsigned j=0;j<4;j++) {
            uint8_t adc[]={32,0,0,0,0,0,15,66,64,0,0,0,0,0,0,0,0};
            /* Physically plausible decoded throttle 0..1 and voltage 1..3.1 V. */
            put32(adc+1,j*333333);put32(adc+5,1000000+j*700000);dispatch(adc,sizeof adc);
            uint8_t current[]={51,0,0,0,8,0,0,0,0};
            current[7]=(uint8_t)((j*1234)>>8);current[8]=(uint8_t)(j*1234);
            dispatch(current,sizeof current);
            safety.valid=true;safety.state= j==3 ? VESC_RIDE_SAFETY_PARK : VESC_RIDE_SAFETY_FORWARD;
            safety.current_profile=(uint8_t)j;
            unsigned before=motor_calls;tick();CHECK(motor_calls==before+1);
            CLOSE(displayed_motor,values[v]/10.0f);CLOSE(displayed_current,j*12.34f);
            CHECK(displayed_gear==(j==3 ? DASH_MODE_PARK : j));
            CLOSE(vesc_io_data_get_latest()->adc1,j*0.333333f);
            CLOSE(vesc_io_data_get_latest()->adc1_voltage,1+j*0.7f);
        }
    }
}
static void freshness_holds_then_recovers(void) {
    temperature(-999);tick();CLOSE(displayed_motor,-99.9f);CHECK(connected);
    unsigned before=motor_calls;now_ms+=5000;tick();
    CHECK(!connected);CHECK(motor_calls==before);CLOSE(displayed_motor,-99.9f);
    temperature(20);tick();CHECK(connected);CHECK(motor_calls==before+1);CLOSE(displayed_motor,2);
}
static void screen_gate(void) {
    temperature(10);tick();unsigned before=motor_calls;
    active_screen=2;temperature(-170);tick();CHECK(motor_calls==before);CLOSE(displayed_motor,1);
    active_screen=1;tick();CHECK(motor_calls==before+1);CLOSE(displayed_motor,-17);
}
static void demo_exit(void) {
    temperature(-999);demo=true;tick();CHECK(motor_calls==0);
    demo=false;tick();CHECK(motor_calls==2);CLOSE(displayed_motor,-99.9f);
    demo=true;tick();now_ms+=5000;demo=false;tick();
    CHECK(motor_calls==3);CLOSE(displayed_motor,0);CHECK(!connected);
}
static void head2_id_age_and_primary(void) {
    float fet=123,mot=456;
    CHECK(!vesc_head2_get_temps(&fet,&mot));CHECK(requested_id==-1);
    secondary_enabled=true;CHECK(!vesc_head2_get_temps(&fet,&mot));CHECK(requested_id==11);
    CLOSE(fet,123);CLOSE(mot,456);
    secondary_present=true;secondary.id=12;secondary.temp_motor=88;secondary.temp_fet=44;
    secondary.rx_time=now_ms;CHECK(!vesc_head2_get_temps(&fet,&mot));
    secondary_id=12;CHECK(vesc_head2_get_temps(&fet,&mot));CLOSE(fet,44);CLOSE(mot,88);
    temperature(-170);tick();CHECK(connected);CLOSE(displayed_motor,-17);
    secondary.temp_motor=-80;tick();CLOSE(displayed_motor,-17);
    secondary.rx_time=now_ms-1000;CHECK(vesc_head2_is_fresh());
    secondary.rx_time=now_ms-1001;CHECK(!vesc_head2_is_fresh());
    unsigned before=motor_calls;temperature(20);tick();CHECK(motor_calls==before);CHECK(!connected);
    secondary_enabled=false;tick();CHECK(connected);CLOSE(displayed_motor,2);
    secondary_enabled=true;now_ms=10;secondary.rx_time=UINT32_MAX-9;
    CHECK(vesc_head2_is_fresh());
}
static void selective_without_temperature_does_not_refresh(void) {
    temperature(-999);tick();now_ms+=6000;CHECK(!vesc_rt_data_is_fresh());
    unsigned before=motor_calls, trips_before=trip_log_calls;
    const uint8_t empty[]={51,0,0,0,0};dispatch(empty,sizeof empty);
    CHECK(!vesc_rt_data_is_fresh());tick();CHECK(motor_calls==before);
    const uint8_t amps[]={51,0,0,0,8,0,0,3,232};dispatch(amps,sizeof amps);
    tick();CHECK(connected);CHECK(motor_calls==before);
    CLOSE(displayed_motor,-99.9f);CLOSE(displayed_current,10);
    /* A fresh FET-only sample must not revive motor temperature. */
    const uint8_t fet[]={51,0,0,0,1,1,144};dispatch(fet,sizeof fet);tick();
    CLOSE(displayed_fet,40);CHECK(motor_calls==before);
    CHECK(trip_log_calls==trips_before+2);
    temperature(-170);tick();CHECK(motor_calls==before+1);CLOSE(displayed_motor,-17);
    CHECK(trip_log_calls==trips_before+3);
}
static void legacy_truncation_rejected(void) {
    temperature(125);tick();now_ms+=6000;
    unsigned before=motor_calls;
    const uint8_t legacy[]={47,1,44,0xfc,0x19};dispatch(legacy,sizeof legacy);tick();
    CHECK(!connected);CHECK(motor_calls==before);CLOSE(displayed_motor,12.5f);
    now_ms+=6000;const uint8_t header[]={47};dispatch(header,sizeof header);tick();
    CHECK(!connected);CHECK(motor_calls==before);CLOSE(displayed_motor,12.5f);
}
static void thermal_age_wrap_reset_inject(void) {
    now_ms=UINT32_MAX-99;temperature(125);tick();
    unsigned before=motor_calls;
    now_ms=100;tick();CHECK(motor_calls==before+1);CLOSE(displayed_motor,12.5f);
    now_ms=4900;
    const uint8_t amps[]={51,0,0,0,8,0,0,3,232};dispatch(amps,sizeof amps);
    before=motor_calls;tick();CHECK(connected);CHECK(motor_calls==before);
    /* Target switch clears temperature validity even if other data arrives. */
    vesc_rt_data_init(11,100);dispatch(amps,sizeof amps);
    tick();CHECK(motor_calls==before);CLOSE(displayed_current,10);
    vesc_setup_values_t injected={0};injected.temp_motor=-17;injected.temp_mos=40;
    vesc_rt_data_inject(&injected);tick();CHECK(motor_calls==before+1);
    CLOSE(displayed_motor,-17);CLOSE(displayed_fet,40);
}
static void realtime_viewer_temperature_age(void) {
    update_cb(NULL);CHECK(!viewer_valid[RT_TMOT]);CHECK(!viewer_valid[RT_TFET]);
    temperature(125);update_cb(NULL);
    CHECK(viewer_valid[RT_TMOT]);CHECK(viewer_valid[RT_TFET]);CLOSE(viewer_value[RT_TMOT],12.5);
    now_ms+=5000;
    const uint8_t amps[]={51,0,0,0,8,0,0,3,232};dispatch(amps,sizeof amps);
    update_cb(NULL);CHECK(viewer_valid[RT_IIN]);CLOSE(viewer_value[RT_IIN],10);
    CHECK(!viewer_valid[RT_TMOT]);CHECK(!viewer_valid[RT_TFET]);
    const uint8_t motor[]={51,0,0,0,2,0xff,0x56};dispatch(motor,sizeof motor);
    update_cb(NULL);CHECK(viewer_valid[RT_TMOT]);CHECK(!viewer_valid[RT_TFET]);
    CLOSE(viewer_value[RT_TMOT],-17);
}
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    vesc_rt_data_init(10,100);vesc_io_data_init(10,150);
#define RUN(name) if(strcmp(argv[1],#name)==0) { name();goto done; }
    RUN(video_values_adc_current_gear_independent)
    RUN(freshness_holds_then_recovers)
    RUN(screen_gate)
    RUN(demo_exit)
    RUN(head2_id_age_and_primary)
    RUN(selective_without_temperature_does_not_refresh)
    RUN(legacy_truncation_rejected)
    RUN(thermal_age_wrap_reset_inject)
    RUN(realtime_viewer_temperature_age)
    return 2;
done:
    printf("%s %s\n",failures ? "FAIL" : "PASS",argv[1]);return failures ? 1 : 0;
}
