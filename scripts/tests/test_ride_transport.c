/* Deterministic execution of the actual transport, with only platform IO
 * mocked. Not a FreeRTOS scheduler or physical CAN timing test.
 * gcc -std=c11 -Wall -Wextra -I scripts/tests/ride_mocks
 * -I components/vesc_can/include scripts/tests/test_ride_transport.c
 * components/vesc_can/vesc_ride_mode_parse.c components/vesc_can/buffer.c
 * -lm -o test_ride_transport
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../components/vesc_can/vesc_ride_mode.c"

static unsigned checks, failures, blocking_sends;
static uint32_t now_ms;
static uint8_t tx[64], tx_id;
static unsigned tx_len;
static bool immediate_reply;
static int lock_values[2], lock_count;
typedef struct { unsigned count, size; uint8_t bytes[4][128]; } queue_t;
static queue_t queue;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

SemaphoreHandle_t xSemaphoreCreateMutex(void) { CHECK(lock_count < 2); return &lock_values[lock_count++]; }
int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{ (void)ticks; if (*sem) { fprintf(stderr, "recursive lock\n"); abort(); } *sem = 1; return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t sem) { *sem = 0; return pdTRUE; }
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{ CHECK(count <= 4 && size <= 128); queue.size = size; queue.count = 0; return &queue; }
int xQueueReset(QueueHandle_t q) { ((queue_t *)q)->count = 0; return pdTRUE; }
int xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{ (void)ticks; queue_t *p = q; if (p->count == 4) return 0; memcpy(p->bytes[p->count++], item, p->size); return pdTRUE; }
int xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{ (void)ticks; queue_t *p = q; if (!p->count) return 0; memcpy(item, p->bytes[0], p->size);
  --p->count; memmove(p->bytes[0], p->bytes[1], p->count * sizeof p->bytes[0]); return pdTRUE; }
int64_t esp_timer_get_time(void) { return (int64_t)now_ms * 1000; }
uint8_t comm_can_get_local_id(void) { return 42; }
void comm_can_send_buffer_sync(uint8_t id, const uint8_t *data,
                              unsigned len, uint8_t send, uint32_t timeout)
{ (void)send; if (*s_lock && timeout) ++blocking_sends;
  CHECK(len <= sizeof tx); memcpy(tx, data, len); tx_len = len; tx_id = id;
  if (immediate_reply && data[3] == VRM_MSG_REQ_SAFETY) {
      uint8_t response[] = {COMM_CUSTOM_APP_DATA, VLP_MAGIC0, VLP_MAGIC1,
          VRM_MSG_SAFETY, VESC_RIDE_SAFETY_VERSION, data[5], data[6],
          VESC_RIDE_SAFETY_PARK, 0, VESC_RIDE_RESULT_OK};
      vesc_ride_mode_process_response(response, sizeof response);
  }
}

static void reply(unsigned id, uint16_t seq, unsigned state)
{
    uint8_t b[] = {COMM_CUSTOM_APP_DATA, VLP_MAGIC0, VLP_MAGIC1,
        (uint8_t)id, VESC_RIDE_SAFETY_VERSION, (uint8_t)(seq >> 8),
        (uint8_t)seq, (uint8_t)state, 0, VESC_RIDE_RESULT_OK};
    vesc_ride_mode_process_response(b, sizeof b);
}
static uint16_t query(void)
{
    now_ms += 200;
    poll_safety(now_ms);
    CHECK(tx_len == 7 && tx[3] == VRM_MSG_REQ_SAFETY);
    uint16_t seq = (uint16_t)((tx[5] << 8) | tx[6]);
    reply(VRM_MSG_SAFETY, seq, VESC_RIDE_SAFETY_PARK);
    return seq;
}
int main(void)
{
    vesc_ride_safety_t st;
    uint16_t seq = 0;
    vesc_ride_mode_init(10);
    CHECK(!vesc_ride_mode_set_park(false, &seq) && seq == 0);
    uint16_t token = query();
    CHECK(vesc_ride_mode_get_safety(&st) && st.valid);
    CHECK(vesc_ride_mode_set_park(false, &seq) && seq == token);
    CHECK(!vesc_ride_mode_set_park(false, NULL));
    /* A reply received before SET was transmitted cannot acknowledge it. */
    reply(VRM_MSG_PARK_ACK, token, VESC_RIDE_SAFETY_FORWARD);
    CHECK(vesc_ride_mode_get_safety(&st) && st.command_pending);
    poll_safety(now_ms);
    CHECK(tx_id == 10 && tx_len == 8 && tx[3] == VRM_MSG_SET_PARK && tx[7] == 0);
    reply(VRM_MSG_SAFETY, token, VESC_RIDE_SAFETY_FORWARD);
    CHECK(vesc_ride_mode_get_safety(&st) && st.command_pending);
    reply(VRM_MSG_PARK_ACK, token + 1, VESC_RIDE_SAFETY_FORWARD);
    CHECK(vesc_ride_mode_get_safety(&st) && st.command_pending);
    reply(VRM_MSG_PARK_ACK, token, VESC_RIDE_SAFETY_FORWARD);
    CHECK(vesc_ride_mode_get_safety(&st) && !st.command_pending && st.valid);
    CHECK(!vesc_ride_mode_set_park(true, NULL)); /* fresh token required */
    query();
    CHECK(vesc_ride_mode_set_park(true, NULL));
    poll_safety(now_ms);
    now_ms += VESC_RIDE_SAFETY_FRESH_MS;
    poll_safety(now_ms);
    CHECK(vesc_ride_mode_get_safety(&st) && !st.valid && !st.command_pending &&
          st.command_result == VESC_RIDE_RESULT_TIMEOUT);
    token = query();
    vesc_ride_mode_set_target(11);
    reply(VRM_MSG_SAFETY, token, VESC_RIDE_SAFETY_FORWARD);
    CHECK(vesc_ride_mode_get_safety(&st) && !st.valid);
    CHECK(!vesc_ride_mode_set_park(false, NULL));
    token = query();
    CHECK(tx_id == 11 && token != 0);
    CHECK(vesc_ride_mode_set_park(false, NULL));
    poll_safety(now_ms);
    vesc_ride_mode_polls_pause(true);
    now_ms += VESC_RIDE_SAFETY_FRESH_MS;
    vesc_ride_mode_poll_loop();
    CHECK(vesc_ride_mode_get_safety(&st) && !st.valid);
    /* Should complete an expired command even if outbound polling is paused. */
    CHECK(!st.command_pending && st.command_result == VESC_RIDE_RESULT_TIMEOUT);
    vesc_ride_mode_polls_pause(false);
    vesc_ride_mode_set_target(12);
    uint8_t old_status[VRM_STATUS_MSG_LEN] = {COMM_CUSTOM_APP_DATA,
        VLP_MAGIC0, VLP_MAGIC1, VRM_MSG_STATUS};
    old_status[13] = 1;
    vesc_ride_mode_process_response(old_status, sizeof old_status);
    vesc_ride_status_t old;
    /* An old/unsolicited status may not validate the new target. */
    CHECK(!vesc_ride_mode_get_status(&old) || !old.valid);
    /* New sequenced STATUS accepts only its current, fresh request. */
    send_status_req();
    CHECK(tx[3] == VRM_MSG_REQ_STATUS_SEQ && tx_len == 7);
    uint8_t status[VRM_STATUS_SEQ_MSG_LEN] = {COMM_CUSTOM_APP_DATA,
        VLP_MAGIC0, VLP_MAGIC1, VRM_MSG_STATUS_SEQ};
    status[4] = tx[5]; status[5] = tx[6]; status[15] = 1;
    vesc_ride_mode_process_response(status, sizeof status);
    CHECK(vesc_ride_mode_get_status(&old) && old.valid);
    now_ms += VESC_RIDE_SAFETY_FRESH_MS;
    CHECK(vesc_ride_mode_get_status(&old) && !old.valid);
    send_status_req();
    uint16_t previous = (uint16_t)((tx[5] << 8) | tx[6]);
    vesc_ride_mode_set_target(13);
    send_status_req();
    CHECK(previous != (uint16_t)((tx[5] << 8) | tx[6]));
    status[4] = (uint8_t)(previous >> 8); status[5] = (uint8_t)previous;
    vesc_ride_mode_process_response(status, sizeof status);
    CHECK(!vesc_ride_mode_get_status(&old));
    status[4] = tx[5]; status[5] = tx[6];
    vesc_ride_mode_process_response(status, sizeof status);
    CHECK(vesc_ride_mode_get_status(&old) && old.valid);
    /* Old queued requests cannot be redirected to the newly selected ESC. */
    vrm_req_t request = {.kind = VRM_REQ_GET, .seq = next_seq()};
    CHECK(enqueue(&request));
    CHECK(xQueueReceive(s_req_q, &request, 0) == pdTRUE);
    vesc_ride_mode_set_target(14);
    tx_len = 0;
    send_get(request.seq, request.generation);
    CHECK(tx_len == 0);
    /* Config packets require a sent query or SET sequence. */
    uint8_t config[VRM_CONFIG_MSG_LEN] = {COMM_CUSTOM_APP_DATA,
        VLP_MAGIC0, VLP_MAGIC1, VRM_MSG_CONFIG, 0, 1, 0,
        VESC_RIDE_CONFIG_FORMAT_VERSION};
    vesc_ride_config_t cfg;
    vesc_ride_mode_process_response(config, sizeof config);
    CHECK(!vesc_ride_mode_get_config(&cfg));
    /* RX can finish before the synchronous send returns, without recursion. */
    immediate_reply = true;
    now_ms += 200;
    poll_safety(now_ms);
    CHECK(vesc_ride_mode_get_safety(&st) && st.valid);
    immediate_reply = false;
    send_get(next_seq(), current_generation());
    config[4] = tx[5]; config[5] = tx[6];
    vesc_ride_mode_process_response(config, sizeof config);
    CHECK(vesc_ride_mode_get_config(&cfg) && cfg.valid);
    vesc_ride_mode_set_target(15);
    vesc_ride_mode_process_response(config, sizeof config);
    CHECK(!vesc_ride_mode_get_config(&cfg));
    /* Real CAN RX dispatch takes s_lock before signalling the sync sender.
     * Holding it while waiting for that signal forces the timeout path. */
    CHECK(blocking_sends == 0);
    printf("Ride transport: %u checks, %u failures; sends while locked=%u\n",
           checks, failures, blocking_sends);
    return failures ? 1 : 0;
}
