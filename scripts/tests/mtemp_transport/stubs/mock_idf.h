#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
static inline void mock_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define ESP_LOGW(...) mock_log(__VA_ARGS__)
#define ESP_LOGI(...) mock_log(__VA_ARGS__)
#define ESP_LOGE(...) mock_log(__VA_ARGS__)
#define ESP_LOGD(...) mock_log(__VA_ARGS__)
static inline const char *esp_err_to_name(int e) { (void)e;return "mock"; }
typedef int gpio_num_t;
#define GPIO_NUM_NC -1
#define TWAI_MODE_NORMAL 0
#define TWAI_STATE_BUS_OFF 1
#define TWAI_STATE_STOPPED 2
typedef struct { int speed; } twai_timing_config_t;
typedef struct { int unused; } twai_filter_config_t;
typedef struct { int tx_io,rx_io,mode,tx_queue_len,rx_queue_len; } twai_general_config_t;
#define TWAI_TIMING_CONFIG_125KBITS() {125}
#define TWAI_TIMING_CONFIG_250KBITS() {250}
#define TWAI_TIMING_CONFIG_500KBITS() {500}
#define TWAI_TIMING_CONFIG_1MBITS() {1000}
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() {0}
#define TWAI_GENERAL_CONFIG_DEFAULT(tx,rx,md) {tx,rx,md,0,0}
typedef struct { bool extd,rtr; uint32_t identifier; uint8_t data_length_code,data[8]; } twai_message_t;
typedef struct {int state;uint32_t tx_error_counter,bus_error_count;} twai_status_info_t;
typedef struct {int mutex,held;} mock_sem;
typedef mock_sem *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
extern uint32_t test_now_ms;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
static mock_sem mock_sems[8];static int mock_sem_count,lock_take,lock_give,wait_count;
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void){return &mock_sems[mock_sem_count++];}
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){SemaphoreHandle_t h=xSemaphoreCreateBinary();h->mutex=1;return h;}
static inline int xSemaphoreTake(SemaphoreHandle_t h,uint32_t t){assert(h);if(h->mutex){assert(!h->held);h->held=1;lock_take++;}else if(t){for(int i=0;i<mock_sem_count;i++)assert(!mock_sems[i].held);wait_count++;}return 1;}
static inline int xSemaphoreGive(SemaphoreHandle_t h){assert(h);if(h->mutex){assert(h->held);h->held=0;lock_give++;}return 1;}
static inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*n,int s,void*a,int p,TaskHandle_t*h,int c){(void)f;(void)n;(void)s;(void)a;(void)p;(void)c;if(h)*h=(void*)1;return 1;}
static inline void vTaskDelay(int t){(void)t;}
static inline void vTaskDelete(void*a){(void)a;}
static inline uint32_t xTaskGetTickCount(void){return test_now_ms;}
static inline void xTaskNotifyGive(TaskHandle_t h){(void)h;}
static inline uint32_t ulTaskNotifyTake(int b,int t){(void)b;(void)t;return 0;}
static inline int64_t esp_timer_get_time(void){return (int64_t)test_now_ms * 1000;}
static twai_message_t sent[12000];static int sent_count,tx_calls,fail_at=-1;
static inline int twai_transmit(const twai_message_t*m,int t){(void)t;int held=0;for(int i=0;i<mock_sem_count;i++)held+=mock_sems[i].held;assert(held==1);if(tx_calls++==fail_at)return ESP_FAIL;sent[sent_count++]=*m;return ESP_OK;}
static inline int twai_driver_install(const void*a,const void*b,const void*c){(void)a;(void)b;(void)c;return ESP_OK;}
static inline int twai_start(void){return ESP_OK;}
static inline int twai_stop(void){return ESP_OK;}
static inline int twai_driver_uninstall(void){return ESP_OK;}
static inline int twai_initiate_recovery(void){return ESP_OK;}
static inline int twai_get_status_info(twai_status_info_t*s){s->state=0;s->tx_error_counter=s->bus_error_count=0;return ESP_OK;}
static inline int twai_receive(twai_message_t*m,int t){(void)m;(void)t;return ESP_FAIL;}
