#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../components/vesc_can/vesc_ride_mode.c"

static unsigned checks, sent;
static uint32_t clock_ms;
static uint8_t last_target, last_packet[64];
static unsigned last_len;
static bool immediate_reply;
static struct { unsigned size, count; unsigned char data[4][128]; } q;
static int mutexes[4], mutex_count;
#define CHECK(x) do { assert(x); ++checks; } while (0)
int64_t esp_timer_get_time(void) { return (int64_t)clock_ms * 1000; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &mutexes[mutex_count++]; }
int xSemaphoreTake(SemaphoreHandle_t m, TickType_t t) {
    (void)t; assert(!*m); *m=1; return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t m) { assert(*m); *m=0; return pdTRUE; }
QueueHandle_t xQueueCreate(unsigned n, unsigned size) {
    assert(n==4 && size<=128); q.size=size; return &q;
}
int xQueueReset(QueueHandle_t h) { (void)h; q.count=0; return pdTRUE; }
int xQueueSend(QueueHandle_t h,const void *p,TickType_t t) {
    (void)h;(void)t; if(q.count==4)return 0;
    memcpy(q.data[q.count++],p,q.size);return pdTRUE;
}
int xQueueReceive(QueueHandle_t h,void *p,TickType_t t) {
    (void)h;(void)t;if(!q.count)return 0;
    memcpy(p,q.data[0],q.size);--q.count;
    memmove(q.data[0],q.data[1],q.count*sizeof q.data[0]);return pdTRUE;
}
uint8_t comm_can_get_local_id(void) {return 2;}
static void reply(uint16_t seq) {
    uint8_t b[28]={36,'V','P',VRM_MSG_CONFIG}; int32_t i=4;
    buffer_append_uint16(b,seq,&i); b[i++]=0; b[i++]=1;
    buffer_append_uint16(b,1,&i);
    for(unsigned m=0;m<3;m++) {
        buffer_append_uint16(b,100+100*m,&i);
        buffer_append_uint16(b,300+300*m,&i);
    }
    b[i++]=1;buffer_append_uint16(b,50,&i);buffer_append_uint16(b,100,&i);b[i++]=0;
    assert(i==28);vesc_ride_mode_process_response(b,sizeof b);
}
void comm_can_send_buffer_sync(uint8_t id,const uint8_t *d,unsigned n,uint8_t send,uint32_t timeout) {
    (void)send;(void)timeout;CHECK(*s_lock==0);CHECK(*s_tx_lock==1);
    ++sent;last_target=id;last_len=n;memcpy(last_packet,d,n);
    if(immediate_reply && (d[3]==VRM_MSG_REQ_CONFIG || d[3]==VRM_MSG_SET_CONFIG))
        reply((uint16_t)((d[5]<<8)|d[6]));
}
int main(void) {
    vesc_ride_config_t c; uint16_t seq;
    vesc_ride_mode_init(10);
    CHECK(vesc_ride_mode_request_config());
    vesc_ride_mode_poll_loop();
    CHECK(sent==1 && last_target==10 && last_packet[3]==VRM_MSG_REQ_CONFIG);
    const uint16_t oldseq=s_query_seq;
    reply(oldseq);CHECK(vesc_ride_mode_get_config(&c));
    CHECK(vesc_ride_mode_set_config(&c,&seq));
    CHECK(vesc_ride_mode_select(2,NULL));
    CHECK(q.count==2 && s_pending_seq==seq);
    vrm_req_t delayed; memcpy(&delayed,q.data[0],q.size);
    vesc_ride_mode_set_screen_active(true);
    vesc_ride_mode_set_target(11);
    CHECK(s_screen_active);
    CHECK(!q.count && !s_pending_seq && !s_have_config && !s_have_status);
    CHECK(!s_first_req_ms && !s_last_config_req_ms && !s_last_status_ms);
    /* Even a request removed just before generation changed cannot send. */
    CHECK(xQueueSend(s_req_q,&delayed,0));
    vesc_ride_mode_poll_loop();CHECK(sent==1);
    reply(oldseq);CHECK(!s_have_config);
    CHECK(vesc_ride_mode_request_config());
    vesc_ride_mode_poll_loop();CHECK(sent==2 && last_target==11);
    CHECK(s_query_seq!=oldseq);reply(oldseq);CHECK(!s_have_config);
    reply(s_query_seq);CHECK(vesc_ride_mode_get_config(&c));
    const unsigned epoch=s_epoch;reply(oldseq);CHECK(s_epoch==epoch);
    immediate_reply=true;
    CHECK(vesc_ride_mode_set_config(&c,&seq));CHECK(s_pending_seq==seq);
    vesc_ride_mode_poll_loop();
    CHECK(!s_pending_seq && !s_write_seq && s_config.response_seq==seq);
    CHECK(last_target==11 && last_packet[3]==VRM_MSG_SET_CONFIG);
    CHECK(last_len==25 && last_packet[7]==1); /* unchanged format1 wire */
    CHECK(vesc_ride_mode_select(1,NULL));vesc_ride_mode_poll_loop();
    CHECK(last_target==11 && last_packet[3]==VRM_MSG_SELECT_MODE);
    vesc_ride_mode_polls_pause(true);clock_ms=1000;
    unsigned before=sent;vesc_ride_mode_poll_loop();CHECK(sent==before);
    vesc_ride_mode_polls_pause(false);vesc_ride_mode_poll_loop();CHECK(sent==before+1);
    for(unsigned n=0;n<4;n++) CHECK(vesc_ride_mode_select(0,NULL));
    const uint16_t pending_before=s_pending_seq;
    CHECK(!vesc_ride_mode_set_config(&c,&seq));
    CHECK(!seq && s_pending_seq==pending_before);
    vesc_ride_mode_set_target(12);CHECK(!q.count);
    CHECK(vesc_ride_mode_request_config());vesc_ride_mode_poll_loop();
    CHECK(last_target==12 && s_have_config); /* immediate RX during TX lock */
    printf("ride target: %u checks passed\n",checks);
    return 0;
}
