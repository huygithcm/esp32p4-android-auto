#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "../../../components/vesc_can/comm_can.c"
static int checks,callbacks;static uint8_t got[1024],sender;static unsigned got_len;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void packet(const uint8_t *p,unsigned n){callbacks++;assert(n<=sizeof(got));memcpy(got,p,n);got_len=n;sender=comm_can_get_packet_sender_id();}
static void reset_tx(void){sent_count=tx_calls=lock_take=lock_give=wait_count=0;fail_at=-1;}
static void reset_rx(void){for(int i=0;i<RX_BUFFER_NUM;i++){s_rx_buffer_device_id[i]=-1;memset(s_rx_buffer[i],0xA5,RX_BUFFER_SIZE);}callbacks=0;}
int main(void){
 CHECK(comm_can_start(20,19,2,500)==ESP_OK);comm_can_set_packet_handler(packet);
 /* Every truncated packet is rejected without touching assembly or dispatch. */
 const int cmds[]={5,6,7,8,17,18,9,14,15,16,58,27};const int mins[]={2,3,6,3,1,1,8,8,8,8,8,6};
 uint8_t bytes[8]={10,1,0,0,0,0,0,0};
 for(unsigned i=0;i<sizeof(cmds)/sizeof(cmds[0]);i++)for(int n=0;n<mins[i];n++){
  reset_rx();reset_tx();decode_msg(((uint32_t)cmds[i]<<8)|2,bytes,n);
  CHECK(callbacks==0&&sent_count==0);CHECK(s_rx_buffer_device_id[0]==-1);CHECK(s_rx_buffer[0][0]==0xA5);
 }
 decode_msg((CAN_PACKET_FILL_RX_BUFFER<<8)|2,NULL,8);decode_msg((CAN_PACKET_FILL_RX_BUFFER<<8)|2,bytes,-1);decode_msg((CAN_PACKET_FILL_RX_BUFFER<<8)|2,bytes,9);
 uint8_t status5[]={0,0,0,123,1,244};decode_msg((CAN_PACKET_STATUS_5<<8)|10,status5,6);
 CHECK(comm_can_get_status_msg_5_id(10)->tacho_value==123);CHECK(comm_can_get_status_msg_5_id(10)->v_in==50.0f);
 uint8_t status1[]={0,0,3,232,0,123,1,244};decode_msg((CAN_PACKET_STATUS<<8)|10,status1,8);
 CHECK(comm_can_get_status_msg_id(10)->rpm==1000.0f);CHECK(comm_can_get_status_msg_id(10)->duty==0.5f);
 uint8_t short_reply[]={10,1,36,0x56,0x50};reset_rx();decode_msg((8<<8)|2,short_reply,5);CHECK(callbacks==1&&got_len==3&&sender==10&&got[0]==36);
 /* Standard, remote and oversized DLC never reach VESC dispatch. */
 twai_message_t frame={0};frame.identifier=(8<<8)|2;frame.data_length_code=5;memcpy(frame.data,short_reply,5);
 reset_rx();process_received_frame(&frame);CHECK(callbacks==0);
 frame.extd=true;frame.rtr=true;process_received_frame(&frame);CHECK(callbacks==0);
 frame.rtr=false;frame.data_length_code=9;process_received_frame(&frame);CHECK(callbacks==0);
 frame.data_length_code=5;process_received_frame(&frame);CHECK(callbacks==1&&sender==10);
 unsigned sizes[]={1,6,7,255,256,259,260,1024};uint8_t payload[1024];for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(uint8_t)(i*17+36);
 for(unsigned j=0;j<sizeof(sizes)/sizeof(sizes[0]);j++){
  unsigned len=sizes[j];reset_tx();reset_rx();comm_can_send_buffer(10,payload,len,1);
  CHECK(lock_take==1&&lock_give==1);CHECK(sent_count>0);
  uint8_t reconstructed[1024]={0};unsigned total=0;
  for(int i=0;i<sent_count;i++){
   twai_message_t *m=&sent[i];unsigned cmd=m->identifier>>8;CHECK(m->extd&&!m->rtr&&(m->identifier&255)==10&&m->data_length_code<=8);
   if(cmd==5){unsigned off=m->data[0],n=m->data_length_code-1;CHECK(off+n<=len);memcpy(reconstructed+off,m->data+1,n);total+=n;}
   if(cmd==6){unsigned off=(m->data[0]<<8)|m->data[1],n=m->data_length_code-2;CHECK(off+n<=len);memcpy(reconstructed+off,m->data+2,n);total+=n;}
   if(cmd==7){CHECK((unsigned)((m->data[2]<<8)|m->data[3])==len);CHECK(((m->data[4]<<8)|m->data[5])==crc16(payload,len));}
   if(cmd==8){memcpy(reconstructed,m->data+2,len);total=len;}
   /* Readdress the exact wire frames as incoming replies; sender is 2. */
   decode_msg((cmd<<8)|2,m->data,m->data_length_code);
  }
  CHECK(total==len&&!memcmp(payload,reconstructed,len));CHECK(callbacks==1&&got_len==len&&!memcmp(got,payload,len)&&sender==2);
 }
 /* CRC failure and invalid terminal lengths clear the occupied slot. */
 reset_tx();comm_can_send_buffer(10,payload,20,1);int frame_count=sent_count;
 for(int mode=0;mode<3;mode++){
  reset_rx();for(int i=0;i<frame_count;i++){twai_message_t m=sent[i];if(i==frame_count-1){if(mode==0)m.data[5]^=1;if(mode==1)m.data[2]=m.data[3]=0;if(mode==2){m.data[2]=4;m.data[3]=1;}}decode_msg((m.identifier&~255u)|2,m.data,m.data_length_code);}
  CHECK(callbacks==0&&s_rx_buffer_device_id[0]==-1);
 }
 /* A full fragmented packet owns one lock, failure aborts without terminal. */
 reset_tx();comm_can_send_buffer(10,payload,1024,0);int count=sent_count;
 for(int f=0;f<count;f++){
  reset_tx();fail_at=f;comm_can_send_buffer_sync(10,payload,1024,0,60);
  CHECK(tx_calls==f+1&&sent_count==f);CHECK(lock_take==1&&lock_give==1&&wait_count==0);
  for(int i=0;i<sent_count;i++)CHECK((sent[i].identifier>>8)!=7);
 }
 reset_tx();comm_can_send_buffer_sync(10,payload,7,0,60);CHECK(wait_count==1&&lock_take==1&&lock_give==1);
 reset_tx();comm_can_send_buffer(10,NULL,2,0);comm_can_send_buffer(10,payload,0,0);comm_can_send_buffer(10,payload,65536,0);CHECK(sent_count==0&&lock_take==0);
 comm_can_transmit_eid(10,payload,9);comm_can_transmit_eid(10,NULL,3);CHECK(sent_count==0&&lock_take==lock_give);
 printf("PASS %d checks: framing, boundaries, sender routing, malformed DLC, CRC, transaction lock and TX failures\n",checks);return 0;
}
