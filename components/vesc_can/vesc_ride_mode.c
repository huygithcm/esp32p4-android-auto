/*
    Copyright 2026 Adapted to ESP-IDF for ESP32-P4 (GPL-3.0).

    Ride-mode and reverse transport. Types, message ids and limits live in
    vesc_ride_mode.h; the wire format and the division of responsibility are in
    docs/RIDE_MODE_REVERSE_BE_CONTRACT.md. In short: the VESC Lisp script owns
    the configuration and every interlock, and this file asks for a copy,
    submits a whole new one when the rider presses Save, and publishes the
    answer.

    There is deliberately no NVS copy on the P4. Two stores are two sources of
    truth, and the one that cannot see the throttle would eventually win an
    argument it has no business being in.

    These are the strong definitions that displace the weak fallbacks in
    Super_VESC_Display/custom/ride_mode_backend_stub.c on a real device.

    Wire format shares the COMM_CUSTOM_APP_DATA / 'VP' channel with
    vesc_lisp_panel.c. The two gate on different message ids and ignore each
    other's traffic.
*/

#include "vesc_can/vesc_ride_mode_wire.h"

#include "vesc_can/buffer.h"
#include "vesc_can/comm_can.h"
#include "vesc_can/vesc_datatypes.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "vesc_ride";

/* Status is eight bytes and carries the direction/armed flags that move under
 * the rider's thumb, so it rides the dashboard cadence. */
#define VRM_STATUS_INTERVAL_MS   200
/* Config is a round trip and almost never changes: requested only while the
 * editor is open, and only until it answers. */
#define VRM_CONFIG_RETRY_MS      600
/* After this long without a single reply the script is presumed to predate
 * 0x07..0x09. Long enough for several retries so a busy bus does not make a
 * working backend look absent. */
#define VRM_TIMEOUT_MS          4000
#define VRM_REQ_QUEUE_LEN          4

typedef enum {
    VRM_REQ_GET = 0,
    VRM_REQ_SET,
    VRM_REQ_SELECT,
} vrm_req_kind_t;

typedef struct {
    vrm_req_kind_t     kind;
    uint16_t           seq;
    uint8_t            profile;   /* SELECT only */
    vesc_ride_config_t cfg;       /* SET only */
} vrm_req_t;

static uint8_t           s_target_vesc_id = 10;
static SemaphoreHandle_t s_lock;
static QueueHandle_t     s_req_q;

/* Guarded by s_lock. */
static vesc_ride_config_t s_config;
static vesc_ride_status_t s_status;
static bool               s_have_config;
static bool               s_have_status;
static uint16_t           s_pending_seq;    /* SET awaiting its reply, 0 = none */
/* One counter feeds both snapshots' epoch, so the screen can tell "nothing
 * moved" from "same numbers, new answer" with a single comparison. */
static uint32_t           s_epoch;

/* Poll-task only, except the volatiles. */
static volatile bool     s_screen_active;
static volatile bool     s_polls_paused;
static uint32_t          s_last_status_ms;
static uint32_t          s_last_config_req_ms;
static uint32_t          s_first_req_ms;    /* 0 until the first request goes out */

/* Sequence numbers are handed out from whichever task calls set/select, so the
 * counter needs its own guard rather than borrowing s_lock, which the LVGL
 * task also takes for snapshots. */
static portMUX_TYPE      s_seq_mux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t          s_seq_next = 1;

static inline uint32_t millis_now(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint16_t next_seq(void)
{
    uint16_t s;
    portENTER_CRITICAL(&s_seq_mux);
    if (s_seq_next == 0) s_seq_next = 1;   /* 0 means "no submission" */
    s = s_seq_next++;
    portEXIT_CRITICAL(&s_seq_mux);
    return s;
}

void vesc_ride_mode_init(uint8_t target_vesc_id)
{
    s_target_vesc_id = target_vesc_id;
    if (!s_lock)  s_lock  = xSemaphoreCreateMutex();
    if (!s_req_q) s_req_q = xQueueCreate(VRM_REQ_QUEUE_LEN, sizeof(vrm_req_t));

    s_screen_active      = false;
    s_polls_paused       = false;
    s_last_status_ms     = 0;
    s_last_config_req_ms = 0;
    s_first_req_ms       = 0;

    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        memset(&s_config, 0, sizeof s_config);
        memset(&s_status, 0, sizeof s_status);
        s_have_config = false;
        s_have_status = false;
        s_pending_seq = 0;
        s_epoch       = 0;
        xSemaphoreGive(s_lock);
    }
}

void vesc_ride_mode_set_target(uint8_t target_vesc_id)
{
    s_target_vesc_id = target_vesc_id;
}

void vesc_ride_mode_set_screen_active(bool active)
{
    s_screen_active = active;
    /* Ask straight away rather than waiting out the retry interval: the editor
     * has nothing to draw until config lands. */
    if (active) s_last_config_req_ms = 0;
}

void vesc_ride_mode_polls_pause(bool paused)
{
    s_polls_paused = paused;
}

/* ---------------------------------------------------------------- requests */

static bool enqueue(const vrm_req_t *req)
{
    if (!s_req_q) return false;
    return xQueueSend(s_req_q, req, 0) == pdTRUE;
}

bool vesc_ride_mode_request_config(void)
{
    vrm_req_t r;
    memset(&r, 0, sizeof r);
    r.kind = VRM_REQ_GET;
    r.seq  = next_seq();
    return enqueue(&r);
}

bool vesc_ride_mode_set_config(const vesc_ride_config_t *cfg, uint16_t *seq_out)
{
    if (seq_out) *seq_out = 0;
    if (!cfg) return false;

    /* Refuse locally what Lisp would refuse anyway. Not a safety gate -- Lisp
     * re-checks with throttle and speed in hand, which this side cannot see --
     * but it turns a bad edit into an immediate answer instead of a round trip
     * that comes back OUT_OF_RANGE a moment later. */
    vesc_ride_result_t why = VESC_RIDE_RESULT_OK;
    if (!vesc_ride_mode_config_in_range(cfg, &why)) {
        if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            s_config.last_result  = why;
            s_config.response_seq = 0;
            s_config.epoch        = ++s_epoch;
            s_pending_seq         = 0;
            xSemaphoreGive(s_lock);
        }
        return false;
    }

    vrm_req_t r;
    memset(&r, 0, sizeof r);
    r.kind = VRM_REQ_SET;
    r.seq  = next_seq();
    r.cfg  = *cfg;
    if (!enqueue(&r)) return false;

    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_pending_seq = r.seq;
        /* The previous answer is not this one. Clearing it stops the screen
         * showing an OK that belongs to the edit before this. */
        s_config.last_result  = VESC_RIDE_RESULT_OK;
        s_config.response_seq = 0;
        s_config.epoch        = ++s_epoch;
        xSemaphoreGive(s_lock);
    }
    if (seq_out) *seq_out = r.seq;
    return true;
}

bool vesc_ride_mode_select(uint8_t profile, uint16_t *seq_out)
{
    if (seq_out) *seq_out = 0;
    if (profile >= VESC_RIDE_MODE_COUNT) return false;

    vrm_req_t r;
    memset(&r, 0, sizeof r);
    r.kind    = VRM_REQ_SELECT;
    r.seq     = next_seq();
    r.profile = profile;
    if (!enqueue(&r)) return false;
    if (seq_out) *seq_out = r.seq;
    return true;
}

/* ------------------------------------------------------------------ sending */

static void send_get(uint16_t seq)
{
    uint8_t buf[8];
    int32_t ind = 0;
    buf[ind++] = COMM_CUSTOM_APP_DATA;
    buf[ind++] = VLP_MAGIC0;
    buf[ind++] = VLP_MAGIC1;
    buf[ind++] = VRM_MSG_REQ_CONFIG;
    buf[ind++] = comm_can_get_local_id();
    buffer_append_uint16(buf, seq, &ind);
    comm_can_send_buffer_sync(s_target_vesc_id, buf, (unsigned int)ind, 0, 60);
}

static void send_select(uint16_t seq, uint8_t profile)
{
    uint8_t buf[8];
    int32_t ind = 0;
    buf[ind++] = COMM_CUSTOM_APP_DATA;
    buf[ind++] = VLP_MAGIC0;
    buf[ind++] = VLP_MAGIC1;
    buf[ind++] = VRM_MSG_SELECT_MODE;
    buf[ind++] = comm_can_get_local_id();
    buffer_append_uint16(buf, seq, &ind);
    buf[ind++] = profile;
    comm_can_send_buffer_sync(s_target_vesc_id, buf, (unsigned int)ind, 0, 60);
}

static void send_set(uint16_t seq, const vesc_ride_config_t *cfg)
{
    uint8_t buf[32];
    int32_t ind = 0;
    buf[ind++] = COMM_CUSTOM_APP_DATA;
    buf[ind++] = VLP_MAGIC0;
    buf[ind++] = VLP_MAGIC1;
    buf[ind++] = VRM_MSG_SET_CONFIG;
    buf[ind++] = comm_can_get_local_id();
    buffer_append_uint16(buf, seq, &ind);
    buf[ind++] = VESC_RIDE_CONFIG_FORMAT_VERSION;
    for (unsigned i = 0; i < VESC_RIDE_MODE_COUNT; i++) {
        buffer_append_uint16(buf, cfg->mode_current_dA[i], &ind);
    }
    buf[ind++] = cfg->reverse_enabled ? 1u : 0u;
    buffer_append_uint16(buf, cfg->reverse_speed_dkmh, &ind);
    buffer_append_uint16(buf, cfg->reverse_current_dA, &ind);
    comm_can_send_buffer_sync(s_target_vesc_id, buf, (unsigned int)ind, 0, 60);
}

/* A read-only poll. The first version of this re-SELECTed the profile it had
 * cached, which is a write dressed as a read: the moment the rider cycled the
 * mode with the ESC's TX button, the next poll asserted the stale profile and
 * put it straight back. The button would have looked broken and the cause
 * would have been five polls a second from the dashboard. */
static void send_status_req(void)
{
    uint8_t buf[8];
    int32_t ind = 0;
    buf[ind++] = COMM_CUSTOM_APP_DATA;
    buf[ind++] = VLP_MAGIC0;
    buf[ind++] = VLP_MAGIC1;
    buf[ind++] = VRM_MSG_REQ_STATUS;
    buf[ind++] = comm_can_get_local_id();
    comm_can_send_buffer_sync(s_target_vesc_id, buf, (unsigned int)ind, 0, 60);
}

void vesc_ride_mode_poll_loop(void)
{
    if (s_polls_paused) return;

    const uint32_t now = millis_now();

    /* Queued work first: a Save the rider is waiting on beats a status poll. */
    vrm_req_t r;
    while (s_req_q && xQueueReceive(s_req_q, &r, 0) == pdTRUE) {
        if (!s_first_req_ms) s_first_req_ms = now ? now : 1;
        switch (r.kind) {
        case VRM_REQ_GET:    send_get(r.seq);               break;
        case VRM_REQ_SET:    send_set(r.seq, &r.cfg);       break;
        case VRM_REQ_SELECT: send_select(r.seq, r.profile); break;
        }
        s_last_config_req_ms = now;
    }

    bool have_cfg = false;
    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        have_cfg = s_have_config;
        xSemaphoreGive(s_lock);
    } else {
        return;
    }

    /* Keep asking only while somebody is looking and nothing has answered. */
    if (s_screen_active && !have_cfg &&
        now - s_last_config_req_ms >= VRM_CONFIG_RETRY_MS) {
        s_last_config_req_ms = now;
        if (!s_first_req_ms) s_first_req_ms = now ? now : 1;
        send_get(next_seq());
        return;
    }

    if (now - s_last_status_ms >= VRM_STATUS_INTERVAL_MS) {
        s_last_status_ms = now;
        if (!s_first_req_ms) s_first_req_ms = now ? now : 1;
        send_status_req();
    }
}

/* ---------------------------------------------------------------- responses */

void vesc_ride_mode_process_response(const uint8_t *data, unsigned int len)
{
    if (!data || len < 4) return;
    if (data[0] != COMM_CUSTOM_APP_DATA) return;
    if (data[1] != VLP_MAGIC0 || data[2] != VLP_MAGIC1) return;

    if (data[3] == VRM_MSG_CONFIG) {
        vesc_ride_config_t cfg;
        if (!vesc_ride_mode_parse_config(data, len, &cfg)) return;

        if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            /* Only the reply to the submission still outstanding may resolve
             * it; anything else would report someone else's answer as ours.
             * An unsolicited reply still refreshes the numbers -- it is the
             * current config either way -- but leaves the pending Save
             * waiting and keeps the result it is waiting for. */
            const bool answers_pending =
                s_pending_seq != 0 && cfg.response_seq == s_pending_seq;
            if (!answers_pending && s_pending_seq != 0) {
                cfg.response_seq = s_config.response_seq;
                cfg.last_result  = s_config.last_result;
            }
            if (answers_pending) s_pending_seq = 0;
            if (cfg.valid || !s_have_config) {
                s_config = cfg;
                s_config.epoch = ++s_epoch;
                if (cfg.valid) s_have_config = true;
            } else {
                /* Keep the last good numbers, publish only the verdict. */
                s_config.response_seq = cfg.response_seq;
                s_config.last_result  = cfg.last_result;
                s_config.epoch        = ++s_epoch;
            }
            xSemaphoreGive(s_lock);
        }
        if (cfg.last_result != VESC_RIDE_RESULT_OK) {
            ESP_LOGW(TAG, "ride config seq=%u refused: result=%u",
                     (unsigned)cfg.response_seq, (unsigned)cfg.last_result);
        }
    } else if (data[3] == VRM_MSG_STATUS) {
        vesc_ride_status_t st;
        if (!vesc_ride_mode_parse_status(data, len, &st)) return;
        if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            st.epoch      = ++s_epoch;
            s_status      = st;
            s_have_status = true;
            xSemaphoreGive(s_lock);
        }
    }
}

/* --------------------------------------------------------------- snapshots */

/* Silence from a script that predates 0x07..0x09 is itself an answer, but only
 * once we have actually asked and waited. Before the first request there is
 * nothing to conclude, so the screen never flashes the banner in the moment
 * between opening and the first reply. */
static bool timed_out(void)
{
    const uint32_t first = s_first_req_ms;
    if (!first) return false;
    return (millis_now() - first) >= VRM_TIMEOUT_MS;
}

bool vesc_ride_mode_get_config(vesc_ride_config_t *out)
{
    if (!out || !s_lock) return false;
    bool ok = false;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        if (s_have_config || s_config.last_result != VESC_RIDE_RESULT_OK) {
            *out = s_config;
            ok = true;
        } else if (timed_out()) {
            /* Report the timeout as a result rather than as "no data", so the
             * screen can say WHY the editor is empty instead of spinning. */
            memset(out, 0, sizeof *out);
            out->valid       = false;
            out->last_result = VESC_RIDE_RESULT_TIMEOUT;
            out->epoch       = s_epoch;
            ok = true;
        }
        xSemaphoreGive(s_lock);
    }
    return ok;
}

bool vesc_ride_mode_get_status(vesc_ride_status_t *out)
{
    if (!out || !s_lock) return false;
    bool ok = false;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        if (s_have_status) { *out = s_status; ok = true; }
        xSemaphoreGive(s_lock);
    }
    return ok;
}
