/* Host fixture around unchanged extracted VESC GPL source and P4 formatter.
 * ADC index assignment below is deliberately synthetic. It tests independent
 * inputs and an explicitly injected shared-input condition, not actual wiring. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/task.h"
#include "vesc_can/comm_can.h"
#include "vesc_can/buffer.h"
#include "vesc_can/vesc_rt_data.h"
#include "vesc_can/vesc_io_data.h"

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); } } while (0)
#define CLOSE(a,b) CHECK(fabsf((a)-(b)) < 0.001f)
static uint32_t now_ms = 1000;
int64_t esp_timer_get_time(void) { return (int64_t)now_ms * 1000; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name,
 uint32_t stack, void *arg, unsigned priority, TaskHandle_t *handle, int core) {
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

static unsigned ADC_Value[3];
#define ADC_IND_TEMP_MOTOR 0
#define ADC_IND_TEMP_MOTOR_2 0
#define ADC_IND_EXT 1
typedef struct { float m_temp_motor, m_temp_override; } motor_t;
typedef struct {
 int m_motor_temp_sens_type;
 float m_ntc_motor_beta, m_ptc_motor_coeff, m_ntcx_ptcx_res, m_ntcx_ptcx_temp_base;
} conf_t;
static char label[32];
static struct { void *dashboard_Classic_temp_mot_text; } guider_ui;
static int s_units_epoch;
static bool fahrenheit, head2_on;
static float head2_motor;
static bool settings_wrapper_get_use_fahrenheit(void) { return fahrenheit; }
static bool dashboard_head2_temps(float *fet, float *mot) { *fet=30;*mot=head2_motor;return head2_on; }
static void dashboard_temps_apply_layout(bool dual) { (void)dual; }
static void lv_label_set_text(void *obj,const char *text) { (void)obj;snprintf(label,sizeof(label),"%s",text); }
#include "extracted.inc"

static conf_t config = {TEMP_SENSOR_NTC_10K_25C,3380.0f,0.6f,10000.0f,25.0f};
static motor_t motor;
static void settle(unsigned adc, unsigned steps) {
 ADC_Value[ADC_IND_TEMP_MOTOR]=adc;
 for (unsigned i=0;i<steps;++i) official_temperature_step(&motor,&config);
 now_ms += steps;
}
static void render(void) {
 uint8_t packet[9]={51}; int32_t i=1;
 buffer_append_uint32(packet,3,&i);
 official_append_float16(packet,30.0f,10.0f,&i);
 official_append_float16(packet,motor.m_temp_motor,10.0f,&i);
 vesc_rt_data_process_response(packet,sizeof(packet));
 cockpit_temp_motor(vesc_rt_data_get_latest()->temp_motor);
 CLOSE(vesc_rt_data_get_latest()->temp_mos,30.0f);
}
static void adc_packet(float level) {
 uint8_t packet[17]={32}; int32_t i=1;
 buffer_append_float32(packet,level,1e6f,&i);
 buffer_append_float32(packet,level*3.3f,1e6f,&i);
 buffer_append_float32(packet,0,1e6f,&i);
 buffer_append_float32(packet,0,1e6f,&i);
 vesc_io_data_process_response(packet,sizeof(packet));
}

int main(void) {
 /* No sensor with the example pull-up drives the ADC to its upper rail.
  * Invalid conversion settles slightly above -100 due to float precision. */
 settle(4095,10000); render();
 CLOSE(motor.m_temp_motor,-99.9996f);
 CLOSE(vesc_rt_data_get_latest()->temp_motor,-99.9f);
 CHECK(strcmp(label,"-99")==0);
 printf("OPEN: filtered=%.7f, wire=%.1f, label=%s\n",motor.m_temp_motor,vesc_rt_data_get_latest()->temp_motor,label);

 /* Distinct throttle input cannot affect the thermal ADC in these macros. */
 for (unsigned k=0;k<=100;++k) {
  ADC_Value[ADC_IND_EXT]=4095*k/100; adc_packet(k/100.0f);
  CLOSE(vesc_io_data_get_latest()->adc1,k/100.0f);
  CLOSE(vesc_io_data_get_latest()->adc1_voltage,k*3.3f/100.0f);
  official_temperature_step(&motor,&config); render();
  CHECK(strcmp(label,"-99")==0);
 }
 printf("PASS independent EXT/ADC32 sweep preserves M-TEMP\n");

 /* -99 is conditional on filtered history/quantization, not a universal
  * renderer clamp. Exact -100 stays -100 on the wire and the display. */
 motor.m_temp_motor=-100.0f;settle(4095,2000);render();
 CHECK(strcmp(label,"-100")==0);
 /* Independent 10k divider control, followed by recovery without reset. */
 settle(2048,2000);render();
 CHECK(fabsf(motor.m_temp_motor-25.0f)<0.02f);
 CHECK(vesc_rt_data_get_latest()->temp_motor>24.8f);
 CHECK(strcmp(label,"24")==0 || strcmp(label,"25")==0);
 printf("PASS exact -100 input renders -100; invalid-to-midpoint recovery gives %.5f C\n",motor.m_temp_motor);

 /* Find independent fixture voltages that reproduce video label values.
  * This deliberately changes TEMP_MOTOR; it does not claim EXT is wired to it. */
 const int targets[]={2,1,2,-99,-17};
 for (unsigned t=0;t<sizeof(targets)/sizeof(targets[0]);++t) {
  unsigned chosen=4095;
  if (targets[t]!=-99) {
   float best=1e9f, desired=targets[t]+(targets[t]<0?-0.5f:0.5f);
   for (unsigned a=1;a<4095;++a) {
    ADC_Value[ADC_IND_TEMP_MOTOR]=a;
    float temp=NTC_TEMP_MOTOR(config.m_ntc_motor_beta);
    float delta=fabsf(temp-desired);
    if(delta<best) { best=delta;chosen=a; }
   }
  }
  settle(chosen,2000);render();
  CHECK(atoi(label)==targets[t]);
  printf("REPLAY: thermal_adc=%u (%.4fV at3.3V), filtered=%.6f, wire=%.1f, label=%s\n",
         chosen,chosen*3.3/4095,motor.m_temp_motor,vesc_rt_data_get_latest()->temp_motor,label);
 }

 /* Disabled uses override, regardless of temperature ADC or EXT. */
 config.m_motor_temp_sens_type=TEMP_SENSOR_DISABLED;
 motor.m_temp_motor=0; motor.m_temp_override=0;
 for(unsigned a=0;a<=4095;a+=63) { settle(a,10);render();CHECK(strcmp(label,"0")==0); }
 motor.m_temp_override=37.5f;settle(4095,2000);render();
 CHECK(strcmp(label,"37")==0);
 printf("PASS Disabled ignores ADC; explicit override controls its temperature\n");

 /* Sensor types other than the NTC example: rail input must never poison LPF. */
 for(int type=TEMP_SENSOR_NTC_10K_25C;type<TEMP_SENSOR_DISABLED;++type) {
  config.m_motor_temp_sens_type=type;
  for(unsigned rail=0;rail<2;++rail) {
   motor.m_temp_motor=0;settle(rail?4095:0,2000);render();
   CHECK(isfinite(motor.m_temp_motor));
   printf("SENSOR type=%d adc=%u -> wire=%.1f label=%s\n",type,rail?4095:0,vesc_rt_data_get_latest()->temp_motor,label);
  }
 }
 config.m_motor_temp_sens_type=TEMP_SENSOR_DISABLED;
 const float bad[]={NAN,INFINITY,-INFINITY,601,-201};
 for(unsigned k=0;k<sizeof(bad)/sizeof(bad[0]);++k) {
  motor.m_temp_motor=0;motor.m_temp_override=bad[k];settle(2048,2000);render();
  CHECK(strcmp(label,"-99")==0);
 }
 printf("PASS invalid values produce the official -100 fallback before filtering\n");

 /* Real UI conversion/rendering, including secondary and unit choice. */
 fahrenheit=true;++s_units_epoch;cockpit_temp_motor(-99.9f);
 CHECK(strcmp(label,"-147")==0);
 fahrenheit=false;head2_on=true;head2_motor=44.4f;++s_units_epoch;
 cockpit_temp_motor(-17.9f);CHECK(strcmp(label,"-17/44")==0);
 head2_on=false;cockpit_temp_motor(-17.9f);CHECK(strcmp(label,"-17")==0);
 printf("PASS production Celsius/Fahrenheit and secondary-head formatter\n");
 printf("Conversion pipeline: %u checks, %u failures; hypothetical ADC input, NO hardware proof\n",checks,failures);
 return failures?1:0;
}
