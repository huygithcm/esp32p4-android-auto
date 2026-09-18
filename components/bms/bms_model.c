/* Normalized BMS snapshot store. See include/bms/bms_model.h for the contract.
 *
 * Readers are the LVGL task (BMS tab) and, later, the phone bridge; the single
 * writer is the BMS worker task. A mutex would be wrong here: the LVGL task
 * must never block behind a BLE worker, and the snapshot is small enough that
 * a critical section costs less than the lock bookkeeping would.
 */

#include "bms/bms_model.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE      s_mux = portMUX_INITIALIZER_UNLOCKED;
static bms_snapshot_t    s_snap;
static bool              s_have_snap;
static uint32_t          s_seq;
static bms_link_state_t  s_link = BMS_LINK_DISABLED;
static bms_diag_t        s_diag;

void bms_model_publish(bms_snapshot_t *snap)
{
    if (!snap) return;

    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_mux);
    snap->sample_seq      = ++s_seq;
    snap->rx_timestamp_us = now;
    s_snap                = *snap;      /* struct copy, no pointers inside */
    s_have_snap           = true;
    portEXIT_CRITICAL(&s_mux);
}

bool bms_model_get(bms_snapshot_t *out)
{
    if (!out) return false;

    bool have;
    portENTER_CRITICAL(&s_mux);
    have = s_have_snap;
    if (have) *out = s_snap;
    portEXIT_CRITICAL(&s_mux);
    return have;
}

uint32_t bms_model_age_ms(void)
{
    int64_t ts;
    bool    have;

    portENTER_CRITICAL(&s_mux);
    have = s_have_snap;
    ts   = s_snap.rx_timestamp_us;
    portEXIT_CRITICAL(&s_mux);

    if (!have) return UINT32_MAX;

    const int64_t delta_us = esp_timer_get_time() - ts;
    if (delta_us < 0) return 0;                      /* clock went backwards */
    const int64_t delta_ms = delta_us / 1000;
    return (delta_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)delta_ms;
}

void bms_model_set_link_state(bms_link_state_t st)
{
    portENTER_CRITICAL(&s_mux);
    s_link = st;
    portEXIT_CRITICAL(&s_mux);
}

bms_link_state_t bms_model_link_state(void)
{
    bms_link_state_t st;
    portENTER_CRITICAL(&s_mux);
    st = s_link;
    portEXIT_CRITICAL(&s_mux);

    /* Fold staleness in here rather than making every caller remember to
     * check the age: a link that is nominally LIVE but has not produced a
     * frame inside the window is, to the rider, not live. */
    if (st == BMS_LINK_LIVE && bms_model_age_ms() > BMS_FRESH_WINDOW_MS) {
        return BMS_LINK_STALE;
    }
    return st;
}

const char *bms_model_link_state_str(bms_link_state_t st)
{
    switch (st) {
    case BMS_LINK_DISABLED:    return "disabled";
    case BMS_LINK_UNBOUND:     return "unbound";
    case BMS_LINK_SCANNING:    return "scanning";
    case BMS_LINK_CONNECTING:  return "connecting";
    case BMS_LINK_PROBING:     return "probing";
    case BMS_LINK_LIVE:        return "live";
    case BMS_LINK_STALE:       return "stale";
    case BMS_LINK_UNSUPPORTED: return "unsupported";
    default:                   return "?";
    }
}

void bms_model_diag_bump(bms_diag_event_t ev)
{
    portENTER_CRITICAL(&s_mux);
    switch (ev) {
    case BMS_DIAG_FRAME_OK:         s_diag.frames_ok++;         break;
    case BMS_DIAG_CRC_ERROR:        s_diag.crc_errors++;        break;
    case BMS_DIAG_LENGTH_ERROR:     s_diag.length_errors++;     break;
    case BMS_DIAG_DROPPED_FRAGMENT: s_diag.dropped_fragments++; break;
    case BMS_DIAG_RECONNECT:        s_diag.reconnects++;        break;
    case BMS_DIAG_TIMEOUT:          s_diag.timeouts++;          break;
    default:                                                    break;
    }
    portEXIT_CRITICAL(&s_mux);
}

const bms_diag_t *bms_model_diag(void)
{
    /* Counters are monotonic and word-sized; a reader that catches one mid
     * increment sees the old or the new value, never garbage. Returning the
     * pointer avoids a copy on a path the UI polls every refresh. */
    return &s_diag;
}

void bms_model_reset(void)
{
    portENTER_CRITICAL(&s_mux);
    memset(&s_snap, 0, sizeof s_snap);
    s_have_snap = false;
    s_link      = BMS_LINK_UNBOUND;
    portEXIT_CRITICAL(&s_mux);
}
