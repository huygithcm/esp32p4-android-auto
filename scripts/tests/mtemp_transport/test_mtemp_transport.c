/* Exercise real comm_can.c (including its static receive decoder) and real
 * RT/IO decoders. TWAI, FreeRTOS and time are the only platform substitutes.
 * Fixtures use VESC CAN framing; no driver task or physical bus is started. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include MTEMP_COMM_CAN_SOURCE
#include "vesc_can/vesc_rt_data.h"
#include "vesc_can/vesc_io_data.h"

uint32_t test_now_ms = 1000;
static int failures, deliveries;
static unsigned delivered_len;
static uint16_t delivered_crc;
#ifdef MTEMP_HAS_SENDER_API
static int delivered_sender = -1;
#endif
static const uint8_t local_id = 30, target_id = 10;
static const uint8_t temp_a[] = {51,0,2,0,3, 1,44, 0,125,10}; /* FET30, motor12.5, ID10 */
static const uint8_t temp_b[] = {51,0,2,0,3, 1,44, 0,20,10}; /* motor2, ID10 */
#define CHECK(t) do { if (!(t)) { failures++; \
    printf("FAIL line %d: %s\n", __LINE__, #t); } } while(0)
#define CLOSE(a,b) CHECK(fabsf((a)-(b)) < 0.0002f)

void vesc_lisp_poll_once_loop(void) {}
void vesc_lisp_panel_dash_loop(void) {}
void vesc_lisp_panel_poll_loop(void) {}
void vesc_ride_mode_poll_loop(void) {}
void vesc_lisp_panel_pas_loop(void) {}

static void dispatch(const uint8_t *data, unsigned len)
{
    deliveries++;
    delivered_len = len;
    delivered_crc = crc16(data, len);
#ifdef MTEMP_HAS_SENDER_API
    CHECK(comm_can_get_packet_sender_id() >= 0);
    delivered_sender = comm_can_get_packet_sender_id();
    vesc_rt_data_process_can_response(data, len, comm_can_get_packet_sender_id());
#else
    vesc_rt_data_process_response(data, len);
#endif
    vesc_io_data_process_response(data, len);
}
static void frame(CAN_PACKET_ID command, uint8_t destination, uint8_t *data, int len)
{ decode_msg(((uint32_t)command << 8) | destination, data, len); }
static void fill(const uint8_t *packet, unsigned offset, unsigned count)
{
    uint8_t bytes[8] = {0};
    CHECK(count <= 7 && offset <= 255);
    bytes[0] = (uint8_t)offset;
    memcpy(bytes + 1, packet + offset, count);
    frame(CAN_PACKET_FILL_RX_BUFFER, local_id, bytes, (int)count+1);
}
static void finish(const uint8_t *packet, unsigned len, uint8_t sender)
{
    uint16_t crc = crc16(packet, len);
    uint8_t bytes[6] = {sender,1,(uint8_t)(len>>8),(uint8_t)len,
                       (uint8_t)(crc>>8),(uint8_t)crc};
    frame(CAN_PACKET_PROCESS_RX_BUFFER, local_id, bytes, 6);
}
static void reply(const uint8_t *packet, unsigned len, uint8_t sender)
{
    for (unsigned off = 0; off < len; off += 7)
        fill(packet, off, len-off < 7 ? len-off : 7);
    finish(packet, len, sender);
}
static void seed(void)
{ reply(temp_a, sizeof(temp_a), target_id); CLOSE(vesc_rt_data_get_latest()->temp_motor,12.5f); }
static void valid_fragmented_video_values(void)
{
    const uint8_t values[][2] = {{0xfc,0x19},{0,20},{0,10},{0xff,0x56}};
    const float expected[] = {-99.9f,2,1,-17};
    for (unsigned n=0;n<4;n++) {
        uint8_t packet[sizeof(temp_a)]; memcpy(packet,temp_a,sizeof(packet));
        memcpy(packet+7,values[n],2); test_now_ms+=100;
        reply(packet,sizeof(packet),target_id);
        CLOSE(vesc_rt_data_get_latest()->temp_motor,expected[n]);
        CLOSE(vesc_rt_data_get_latest()->temp_mos,30);
        CHECK(vesc_rt_data_get_latest()->rx_time == test_now_ms);
    }
    CHECK(deliveries==4);
}
static void adc_custom_interleaving(void)
{
    seed();
    const uint8_t adc[][17] = {
        {32,0,0,0,0,0,15,66,64,0,0,0,0,0,0,0,0},
        {32,0,7,161,32,0,30,132,128,0,0,0,0,0,0,0,0},
        {32,0,15,66,64,0,45,198,192,0,0,0,0,0,0,0,0}};
    /* From lisp/main.lisp safety-send: VP/8B, version1, seq1,
     * state FAULT(5), profile0, INPUT_FAULT(12). */
    const uint8_t custom[] = {36,0x56,0x50,0x8b,1,0,1,5,0,12};
    /* rm-send-status-seq: 19-byte VP payload plus command byte. */
    const uint8_t status[] = {36,0x56,0x50,0x8d,0,1,0,1,0,
                             0,50,0,50,0,100,1,0,0,0,12};
    for(unsigned n=0;n<3;n++) {
        vesc_setup_values_t before=*vesc_rt_data_get_latest();
        test_now_ms+=100; reply(adc[n],17,target_id); reply(custom,sizeof(custom),target_id);
        reply(status,sizeof(status),target_id);
        CHECK(memcmp(&before,vesc_rt_data_get_latest(),sizeof(before))==0);
        CLOSE(vesc_io_data_get_latest()->adc1,n*0.5f);
        reply(temp_a,sizeof(temp_a),target_id);
        CLOSE(vesc_rt_data_get_latest()->temp_motor,12.5f);
    }
}
static void overlapping_replies_crc_drop(void)
{
    seed(); int before=deliveries; uint32_t time=vesc_rt_data_get_latest()->rx_time;
    const uint8_t adc[] = {32,0,15,66,64,0,45,198,192,0,0,0,0,0,0,0,0};
    fill(temp_b,0,7); fill(adc,0,7); fill(temp_b,7,3);
    test_now_ms+=100; finish(temp_b,sizeof(temp_b),target_id);
    CHECK(deliveries==before); CHECK(vesc_rt_data_get_latest()->rx_time==time);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,12.5f);
    reply(temp_b,sizeof(temp_b),target_id); CLOSE(vesc_rt_data_get_latest()->temp_motor,2);
}
static void missing_changed_tail_crc_drop(void)
{
    seed(); int before=deliveries;
    fill(temp_b,0,7); test_now_ms+=100; finish(temp_b,sizeof(temp_b),target_id);
    CHECK(deliveries==before); CLOSE(vesc_rt_data_get_latest()->temp_motor,12.5f);
}
static void missing_unchanged_tail_refresh(void)
{
    seed(); int before=deliveries; uint32_t time=vesc_rt_data_get_latest()->rx_time;
    fill(temp_a,0,7); test_now_ms+=4000; finish(temp_a,sizeof(temp_a),target_id);
    printf("OBSERVE missing temperature tail: deliveries %d -> %d; sample time %u -> %u\n",
           before,deliveries,time,vesc_rt_data_get_latest()->rx_time);
    CHECK(deliveries==before); CHECK(vesc_rt_data_get_latest()->rx_time==time);
}
static void missing_tail_numeric_replay(void)
{
    /* ID0 keeps the overlapping byte7 equal to A's motor high byte, while B
     * updates motor to -17. Retained A tail plus A's CRC can pass pre-fix. */
    vesc_rt_data_init(0,100);
    uint8_t original[sizeof(temp_a)]; memcpy(original,temp_a,sizeof(original));
    original[9]=0;
    reply(original,sizeof(original),0);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,12.5f);
    const uint8_t motor_only[] = {51,0,2,0,2,0xff,0x56,0}; /* motor -17, ID0 */
    reply(motor_only,sizeof(motor_only),0);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,-17);
    fill(original,0,7); test_now_ms+=100; finish(original,sizeof(original),0);
    printf("OBSERVE missing temperature tail: -17 -> %.1f C (retained bytes from older reply)\n",
           vesc_rt_data_get_latest()->temp_motor);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,-17);
}
static void wrong_sender_temperature(void)
{
    seed(); test_now_ms+=100; int before=deliveries;
    vesc_setup_values_t snapshot=*vesc_rt_data_get_latest();
    uint8_t other[sizeof(temp_b)]; memcpy(other,temp_b,sizeof(other)); other[9]=11;
    reply(other,sizeof(other),11);
    printf("OBSERVE target=10, reply sender=11, motorID=11: 12.5 -> %.1f C\n",
           vesc_rt_data_get_latest()->temp_motor);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,12.5f);
    CHECK(memcmp(&snapshot,vesc_rt_data_get_latest(),sizeof(snapshot))==0);
    CHECK(deliveries==before+1); /* Raw bridge consumer still sees other nodes. */
}
static void dual_motor_base_sender(void)
{
    vesc_rt_data_init(11,100);
    uint8_t second[sizeof(temp_b)]; memcpy(second,temp_b,sizeof(second)); second[9]=11;
    reply(second,sizeof(second),10);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,2);
    CHECK(vesc_rt_data_get_latest()->vesc_id==11); CHECK(deliveries==1);
}
static void dual_motor_sender_wrap(void)
{
    vesc_rt_data_init(0,100);
    uint8_t second[sizeof(temp_b)]; memcpy(second,temp_b,sizeof(second)); second[9]=0;
    reply(second,sizeof(second),254);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,2);
    CHECK(vesc_rt_data_get_latest()->vesc_id==0); CHECK(deliveries==1);
}
static void matching_sender_wrong_motor(void)
{
    seed(); int before=deliveries;
    vesc_setup_values_t snapshot=*vesc_rt_data_get_latest();
    uint8_t second[sizeof(temp_b)]; memcpy(second,temp_b,sizeof(second)); second[9]=11;
    reply(second,sizeof(second),10);
    CHECK(memcmp(&snapshot,vesc_rt_data_get_latest(),sizeof(snapshot))==0);
    CHECK(deliveries==before+1);
}
static void inconsistent_sender_identity(void)
{
    seed(); int before=deliveries;
    vesc_setup_values_t snapshot=*vesc_rt_data_get_latest();
    reply(temp_b,sizeof(temp_b),11);
    CHECK(memcmp(&snapshot,vesc_rt_data_get_latest(),sizeof(snapshot))==0);
    CHECK(deliveries==before+1);
}
static void missing_motor_identity(void)
{
    seed(); int before=deliveries;
    vesc_setup_values_t snapshot=*vesc_rt_data_get_latest();
    const uint8_t unidentified[]={51,0,0,0,3,1,44,0,20};
    reply(unidentified,sizeof(unidentified),10);
    CHECK(memcmp(&snapshot,vesc_rt_data_get_latest(),sizeof(snapshot))==0);
    CHECK(deliveries==before+1);
}
static void status4_is_separate(void)
{
    seed(); vesc_setup_values_t before=*vesc_rt_data_get_latest();
    uint8_t status[]={1,44,0xff,0x56,0,0,0,0};
    frame(CAN_PACKET_STATUS_4,target_id,status,8);
    CHECK(comm_can_get_status_msg_4_id(target_id)!=NULL);
    CLOSE(comm_can_get_status_msg_4_id(target_id)->temp_motor,-17);
    CHECK(memcmp(&before,vesc_rt_data_get_latest(),sizeof(before))==0);
}
static void short_status4_dlc(void)
{
    seed();
    /* Eight initialized storage bytes prevent UB. DLC=2: bytes 2..7 are
     * deliberately stale backing storage, NOT bytes received on this frame. */
    uint8_t storage[]={1,44,0xff,0x56,0,0,0,0};
    frame(CAN_PACKET_STATUS_4,target_id,storage,2);
    can_status_msg_4 *status=comm_can_get_status_msg_4_id(target_id);
    printf("OBSERVE STATUS4 DLC=2: stored=%s motor=%.1f; RT=%.1f C\n",
           status ? "yes":"no", status ? status->temp_motor:0,
           vesc_rt_data_get_latest()->temp_motor);
    CHECK(status==NULL); CLOSE(vesc_rt_data_get_latest()->temp_motor,12.5f);
}
static void no_prefix_or_duplicate_finish(void)
{
    seed(); int before=deliveries;
    finish(temp_a,sizeof(temp_a),target_id);
    fill(temp_a,7,3); finish(temp_a,sizeof(temp_a),target_id);
    CHECK(deliveries==before);
    reply(temp_b,sizeof(temp_b),target_id); CHECK(deliveries==before+1);
}
static void out_of_order_fragments(void)
{
    const uint8_t packet[17]={32,0,15,66,64,0,45,198,192,0,0,0,0,0,0,0,0};
    seed(); int before=deliveries;
    fill(packet,0,7); fill(packet,14,3); fill(packet,7,7);
    finish(packet,sizeof(packet),target_id); CHECK(deliveries==before);
    reply(packet,sizeof(packet),target_id); CHECK(deliveries==before+1);
}
static void duplicate_fragment_recovery(void)
{
    seed(); int before=deliveries;
    fill(temp_b,0,7); fill(temp_b,7,3); fill(temp_b,7,3);
    finish(temp_b,sizeof(temp_b),target_id); CHECK(deliveries==before);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,12.5f);
    reply(temp_b,sizeof(temp_b),target_id); CHECK(deliveries==before+1);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,2);
}
static void long_fragment_roundtrip(void)
{
    uint8_t packet[RX_BUFFER_SIZE];
    for(unsigned i=0;i<sizeof(packet);i++)packet[i]=(uint8_t)(i*17);
    packet[0]=COMM_GET_MCCONF;
    unsigned offset=0;
    for(;offset<=255;offset+=7)fill(packet,offset,7);
    for(;offset<sizeof(packet);offset+=6){
        unsigned count=sizeof(packet)-offset; if(count>6)count=6;
        uint8_t bytes[8]={(uint8_t)(offset>>8),(uint8_t)offset};
        memcpy(bytes+2,packet+offset,count);
        frame(CAN_PACKET_FILL_RX_BUFFER_LONG,local_id,bytes,(int)count+2);
    }
    finish(packet,sizeof(packet),target_id); CHECK(deliveries==1);
    CHECK(delivered_len==sizeof(packet)); CHECK(delivered_crc==crc16(packet,sizeof(packet)));
}
static void malformed_control_dlc(void)
{
    seed(); int before=deliveries;
    uint8_t bytes[8]={target_id,1,0,9,0,0,0,0};
    for(int len=0;len<6;len++)frame(CAN_PACKET_PROCESS_RX_BUFFER,local_id,bytes,len);
    for(int len=0;len<3;len++)frame(CAN_PACKET_PROCESS_SHORT_BUFFER,local_id,bytes,len);
    uint8_t zero[8]={0};
    for(int len=0;len<2;len++)frame(CAN_PACKET_FILL_RX_BUFFER,local_id,zero,len);
    for(int len=0;len<3;len++)frame(CAN_PACKET_FILL_RX_BUFFER_LONG,local_id,zero,len);
    frame(CAN_PACKET_PROCESS_SHORT_BUFFER,local_id,bytes,9);
    CHECK(deliveries==before);
    reply(temp_b,sizeof(temp_b),target_id); CHECK(deliveries==before+1);
}
static void overflow_and_recovery(void)
{
    seed(); int before=deliveries;
    fill(temp_a,0,7);
    uint8_t overflow[8]={0xff,0xff,1,2,3,4,5,6};
    frame(CAN_PACKET_FILL_RX_BUFFER_LONG,local_id,overflow,8);
    finish(temp_a,sizeof(temp_a),target_id); CHECK(deliveries==before);
    reply(temp_b,sizeof(temp_b),target_id); CHECK(deliveries==before+1);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,2);
}
static void sender_scope_short_bridge(void)
{
    seed(); int before=deliveries;
#ifdef MTEMP_HAS_SENDER_API
    CHECK(comm_can_get_packet_sender_id()==-1);
#endif
    uint8_t bytes[3]={11,1,COMM_PRINT};
    frame(CAN_PACKET_PROCESS_SHORT_BUFFER,local_id,bytes,3);
    CHECK(deliveries==before+1); CHECK(delivered_len==1);
#ifdef MTEMP_HAS_SENDER_API
    CHECK(delivered_sender==11); CHECK(comm_can_get_packet_sender_id()==-1);
#endif
    CLOSE(vesc_rt_data_get_latest()->temp_motor,12.5f);
}
static void status_dlc_validation(void)
{
    uint8_t bytes[8]={0};
    const CAN_PACKET_ID ids[]={CAN_PACKET_STATUS,CAN_PACKET_STATUS_2,
        CAN_PACKET_STATUS_3,CAN_PACKET_STATUS_4,CAN_PACKET_STATUS_5,CAN_PACKET_STATUS_6};
    for(unsigned i=0;i<6;i++){
        int size=i==4?6:8;
        for(int len=0;len<size;len++)frame(ids[i],target_id,bytes,len);
    }
    CHECK(comm_can_get_status_msg_id(target_id)==NULL);
    CHECK(comm_can_get_status_msg_2_id(target_id)==NULL);
    CHECK(comm_can_get_status_msg_3_id(target_id)==NULL);
    CHECK(comm_can_get_status_msg_4_id(target_id)==NULL);
    CHECK(comm_can_get_status_msg_5_id(target_id)==NULL);
    CHECK(comm_can_get_status_msg_6_id(target_id)==NULL);
    for(unsigned i=0;i<6;i++)frame(ids[i],target_id,bytes,i==4?6:8);
    CHECK(comm_can_get_status_msg_id(target_id)!=NULL);
    CHECK(comm_can_get_status_msg_2_id(target_id)!=NULL);
    CHECK(comm_can_get_status_msg_3_id(target_id)!=NULL);
    CHECK(comm_can_get_status_msg_4_id(target_id)!=NULL);
    CHECK(comm_can_get_status_msg_5_id(target_id)!=NULL);
    CHECK(comm_can_get_status_msg_6_id(target_id)!=NULL);
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    s_can_config.controller_id=local_id;
    for(int i=0;i<RX_BUFFER_NUM;i++)s_rx_buffer_device_id[i]=-1;
    for(int i=0;i<CAN_STATUS_MSGS_TO_STORE;i++)stat_msgs_4[i].id=-1;
    for(int i=0;i<CAN_STATUS_MSGS_TO_STORE;i++){
        stat_msgs[i].id=stat_msgs_2[i].id=stat_msgs_3[i].id=-1;
        stat_msgs_5[i].id=stat_msgs_6[i].id=-1;
    }
    comm_can_set_packet_handler(dispatch);
    vesc_rt_data_init(target_id,100); vesc_io_data_init(target_id,150);
#define RUN(name) if(strcmp(argv[1],#name)==0){name();printf("%s %s\n",failures?"FAIL":"PASS",#name);return failures?1:0;}
    RUN(valid_fragmented_video_values) RUN(adc_custom_interleaving)
    RUN(overlapping_replies_crc_drop) RUN(missing_changed_tail_crc_drop)
    RUN(missing_unchanged_tail_refresh) RUN(missing_tail_numeric_replay)
    RUN(wrong_sender_temperature) RUN(status4_is_separate) RUN(short_status4_dlc)
    RUN(no_prefix_or_duplicate_finish) RUN(out_of_order_fragments)
    RUN(long_fragment_roundtrip) RUN(malformed_control_dlc) RUN(status_dlc_validation)
    RUN(overflow_and_recovery) RUN(sender_scope_short_bridge)
    RUN(duplicate_fragment_recovery)
    RUN(dual_motor_base_sender) RUN(matching_sender_wrong_motor)
    RUN(inconsistent_sender_identity) RUN(missing_motor_identity)
    RUN(dual_motor_sender_wrap)
    return 2;
}
