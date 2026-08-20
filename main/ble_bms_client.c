#include "ble_bms_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bms/bms_jk.h"
#include "bms/bms_model.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"

static const char *TAG = "ble_bms";

/* JK exposes one service with one characteristic that is both the command
 * sink and the notification source. */
static const ble_uuid_t *JK_SVC_UUID = BLE_UUID16_DECLARE(0xFFE0);
static const ble_uuid_t *JK_CHR_UUID = BLE_UUID16_DECLARE(0xFFE1);
static const ble_uuid_t *CCCD_UUID   = BLE_UUID16_DECLARE(0x2902);

#define SCAN_DURATION_MS   10000
/* This driver does NOT poll. A JK pack beeps on every command it receives, so
 * asking once a second turns the rider's battery into a metronome for as long
 * as the tab is open. It does not need asking: once the CCCD is written and
 * the first command has been answered, the pack pushes cell-info frames by
 * itself at its own cadence (about 1 Hz). One kick, then listen.
 *
 * What remains is a watchdog. If the stream dries up we send one command to
 * restart it -- one beep, and only when something is already wrong. The window
 * is deliberately much wider than the pack's own interval so an ordinary
 * skipped frame never triggers it. */
#define STREAM_SILENCE_MS  15000
/* Probing for the layout is different: nothing streams until the pack has
 * answered device-info, so that one really does have to be repeated. */
#define PROBE_RETRY_MS     3000
/* How often the worker re-arms a connect that never got off the ground.
 * ble_gap_connect can be refused outright -- NimBLE runs one initiator, so a
 * scan or the cadence client holding it returns an error immediately rather
 * than through the GAP callback. Every other retry in this module is
 * event-driven, and there is no event for "the call failed", so without this
 * tick a refusal at boot is permanent. Five seconds is far longer than any
 * competing procedure and invisible next to the ~30 s connect timeout. */
#define RECONNECT_RETRY_MS 5000

/* One frame is 300 bytes; size the pipe for a couple of them so a slow
 * worker cannot lose a burst. */
#define RX_STREAM_BYTES    1024

static uint8_t             s_own_addr_type;
static bool                s_inited;
static bool                s_synced;
static bool                s_bound;
static ble_addr_t          s_bound_addr;
static uint16_t            s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t            s_chr_val_handle;
static uint16_t            s_chr_end_handle;
static uint16_t            s_cccd_handle;
static bool                s_scanning;
static bool                s_connecting;
static bool                s_subscribed;
/* Bumped by reset_link_state() on every teardown. The worker keeps its own
 * copy so it can tell a new session from a continuing one without relying on
 * having observed s_subscribed go false -- which polling at 200 ms would
 * almost certainly catch, but "almost" is not a guarantee and the cost of
 * missing it is a session that never kicks its stream. */
static volatile uint32_t   s_link_gen;
static ble_bms_scan_cb_t   s_scan_cb;
static StreamBufferHandle_t s_rx;
static TaskHandle_t        s_worker;
static jk_ctx_t            s_jk;
static int8_t              s_rssi_dbm;
static bool                s_logged_full;
static char                s_peer_name[32];
/* Set by bind(), consumed by the worker task: ble_bms_bind runs on the LVGL
 * task when the user taps a scan hit, and an nvs_commit there is the exact
 * freeze this project's CLAUDE.md warns about. The worker owns NVS instead. */
static volatile bool       s_store_peer;
static bool                s_peer_persisted;
static volatile bool       s_erase_peer;
/* Starts false to match bms_view's own initial state — see the header. */
static bool                s_active;

/* The bound peer lives in a namespace of this module's own rather than in
 * dev_settings. It is backend state, dev_settings is an upstream file the FE
 * also edits, and keeping it here costs one NVS handle and no merge conflict. */
#define BMS_NVS_NS   "bms_ble"
#define BMS_NVS_KEY  "peer"

static void peer_store(const ble_addr_t *addr)
{
    nvs_handle_t h;
    if (nvs_open(BMS_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (addr) nvs_set_blob(h, BMS_NVS_KEY, addr, sizeof *addr);
    else      nvs_erase_key(h, BMS_NVS_KEY);
    nvs_commit(h);
    nvs_close(h);
}

static bool peer_load(ble_addr_t *out)
{
    nvs_handle_t h;
    if (nvs_open(BMS_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof *out;
    const esp_err_t e = nvs_get_blob(h, BMS_NVS_KEY, out, &len);
    nvs_close(h);
    return e == ESP_OK && len == sizeof *out;
}

static int  bms_gap_event(struct ble_gap_event *event, void *arg);
static void arm_connect(void);

static void reset_link_state(void)
{
    s_conn           = BLE_HS_CONN_HANDLE_NONE;
    s_chr_val_handle = 0;
    s_chr_end_handle = 0;
    s_cccd_handle    = 0;
    s_subscribed     = false;
    s_logged_full    = false;
    s_link_gen++;
    s_connecting     = false;
    jk_init(&s_jk);
}

/* ---------------------------------------------------------------- commands */

static int send_cmd(uint8_t cmd)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_chr_val_handle) return -1;

    uint8_t frame[JK_CMD_LEN];
    if (jk_build_cmd(frame, sizeof frame, cmd, 0) != JK_CMD_LEN) return -1;

    /* Write without response: JK answers on the notification path, and a
     * write-response round trip would only add latency. */
    const int rc = ble_gattc_write_no_rsp_flat(s_conn, s_chr_val_handle,
                                               frame, sizeof frame);
    if (rc != 0) ESP_LOGW(TAG, "write cmd 0x%02x rc=%d", cmd, rc);
    return rc;
}

/* ---------------------------------------------------------------- discovery */

static int on_subscribed(uint16_t conn, const struct ble_gatt_error *err,
                         struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (err->status != 0) {
        ESP_LOGW(TAG, "CCCD write failed status=%d", err->status);
        bms_model_set_link_state(BMS_LINK_UNSUPPORTED);
        return 0;
    }
    s_subscribed = true;
    bms_model_set_link_state(BMS_LINK_PROBING);
    ESP_LOGI(TAG, "subscribed; asking for device info");

    /* Device info first, always. The cell-info layout differs by 32 bytes
     * between hardware generations and the only way to know which one this
     * unit speaks is its version string. Decoding cell data before that
     * would publish numbers that look sane and are wrong. */
    send_cmd(JK_CMD_DEVICE_INFO);
    return 0;
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *err,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                  void *arg)
{
    (void)chr_val_handle; (void)arg;
    if (err->status == 0 && dsc && ble_uuid_cmp(&dsc->uuid.u, CCCD_UUID) == 0 &&
        !s_cccd_handle) {
        s_cccd_handle = dsc->handle;
        static const uint8_t en[2] = { 0x01, 0x00 };   /* notifications on */
        const int rc = ble_gattc_write_flat(conn, s_cccd_handle, en, sizeof en,
                                            on_subscribed, NULL);
        if (rc != 0) ESP_LOGW(TAG, "ble_gattc_write_flat rc=%d", rc);
    }
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err,
                  const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (err->status == 0 && chr) {
        s_chr_val_handle = chr->val_handle;
        return 0;
    }
    /* Discovery of this characteristic finished. */
    if (s_chr_val_handle) {
        ble_gattc_disc_all_dscs(conn, s_chr_val_handle, s_chr_end_handle,
                                on_dsc, NULL);
    } else {
        ESP_LOGW(TAG, "0xFFE1 not found — not a JK BMS?");
        bms_model_set_link_state(BMS_LINK_UNSUPPORTED);
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err,
                  const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (err->status == 0 && svc) {
        s_chr_end_handle = svc->end_handle;
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle,
                                    JK_CHR_UUID, on_chr, NULL);
    } else if (!s_chr_end_handle) {
        ESP_LOGW(TAG, "0xFFE0 not found — not a JK BMS?");
        bms_model_set_link_state(BMS_LINK_UNSUPPORTED);
    }
    return 0;
}

static void start_discovery(uint16_t conn)
{
    s_chr_val_handle = 0;
    s_chr_end_handle = 0;
    s_cccd_handle    = 0;
    ble_gattc_disc_svc_by_uuid(conn, JK_SVC_UUID, on_svc, NULL);
}

/* ------------------------------------------------------------------ scanning */

/* JK advertises names like "JK-B2A24S15P" / "JK_BMS". Match the prefix rather
 * than a service UUID: several JK generations advertise no 0xFFE0 in the
 * advertisement at all and only expose it after connecting. */
static bool adv_looks_like_jk(const struct ble_hs_adv_fields *f, char *name_out,
                              size_t name_cap)
{
    const uint8_t n = f->name_len;
    if (!f->name || n == 0) return false;

    size_t copy = (n < name_cap - 1) ? n : name_cap - 1;
    memcpy(name_out, f->name, copy);
    name_out[copy] = '\0';

    return (n >= 3) && (name_out[0] == 'J' || name_out[0] == 'j') &&
           (name_out[1] == 'K' || name_out[1] == 'k') &&
           (name_out[2] == '-' || name_out[2] == '_');
}

/* ---------------------------------------------------------------- GAP events */

static int bms_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields f;
        if (ble_hs_adv_parse_fields(&f, event->disc.data,
                                    event->disc.length_data) != 0) {
            return 0;
        }
        char name[32];
        if (!adv_looks_like_jk(&f, name, sizeof name)) return 0;
        /* Remember the level for the peer we are actually bound to, so the
         * UI has something to show before the link is up. */
        if (s_bound && memcmp(&event->disc.addr, &s_bound_addr,
                              sizeof s_bound_addr) == 0) {
            s_rssi_dbm = event->disc.rssi;
            /* Remember what it calls itself; the frames never say. */
            snprintf(s_peer_name, sizeof s_peer_name, "%s", name);
        }
        if (s_scan_cb) s_scan_cb(&event->disc.addr, name, event->disc.rssi);
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_scanning = false;
        /* The scan window closing is the only signal the UI gets that the
         * sweep finished. Left in SCANNING the tab spins forever, which reads
         * as a hung radio rather than "nothing answered". Only fall back when
         * no link took over in the meantime. */
        if (s_conn == BLE_HS_CONN_HANDLE_NONE && !s_connecting) {
            bms_model_set_link_state(s_bound ? BMS_LINK_STALE
                                             : BMS_LINK_UNBOUND);
        }
        return 0;

#if defined(BLE_GAP_EVENT_LINK_ESTAB)
    case BLE_GAP_EVENT_LINK_ESTAB:
#endif
    case BLE_GAP_EVENT_CONNECT:
        s_connecting = false;
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected, conn=%u", (unsigned)s_conn);
            /* A bigger MTU turns a 300-byte frame from ~15 notifications
             * into one or two. NimBLE is configured for 512 already; the
             * BMS may or may not agree, and either way works. */
            ble_gattc_exchange_mtu(s_conn, NULL, NULL);
            start_discovery(s_conn);
        } else {
            ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
            bms_model_set_link_state(BMS_LINK_CONNECTING);
            arm_connect();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "disconnected reason=%d", event->disconnect.reason);
        reset_link_state();
        bms_model_diag_bump(BMS_DIAG_RECONNECT);
        bms_model_set_link_state(s_bound ? BMS_LINK_CONNECTING
                                         : BMS_LINK_UNBOUND);
        arm_connect();
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.attr_handle != s_chr_val_handle) return 0;
        const uint16_t n = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (n == 0 || !s_rx) return 0;

        /* Keep this callback short: copy out of the mbuf and leave. Parsing a
         * 300-byte frame here would run on the NimBLE host task and stall
         * every other BLE link, including the phone app.
         *
         * The buffer must hold a COMPLETE frame: we request MTU 512+, so the
         * whole 300-byte reply arriving as one notification is the normal
         * case, not the exception. At 256 this truncated 44 bytes off every
         * such frame and each one died at the checksum — found in Codex's
         * integration audit before it could burn a hardware session. */
        uint8_t tmp[JK_FRAME_LEN + 20];
        const uint16_t take = (n > sizeof tmp) ? sizeof tmp : n;
        if (ble_hs_mbuf_to_flat(event->notify_rx.om, tmp, take, NULL) != 0) {
            return 0;
        }
        if (xStreamBufferSend(s_rx, tmp, take, 0) != take) {
            bms_model_diag_bump(BMS_DIAG_DROPPED_FRAGMENT);
        }
        return 0;
    }

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU now %u", (unsigned)event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void arm_connect(void)
{
    if (!s_inited || !s_synced || !s_bound) return;
    if (s_connecting || s_conn != BLE_HS_CONN_HANDLE_NONE) return;

    s_connecting = true;
    bms_model_set_link_state(BMS_LINK_CONNECTING);
    const int rc = ble_gap_connect(s_own_addr_type, &s_bound_addr, 30000, NULL,
                                   bms_gap_event, NULL);
    if (rc != 0) {
        s_connecting = false;
        ESP_LOGW(TAG, "ble_gap_connect rc=%d", rc);
    }
}

/* ------------------------------------------------------------------ logging */

/* Bring-up aid. Nothing in the BMS path has been verified against a real pack,
 * and the failure that matters — decoding with the wrong 24S/32S layout — puts
 * believable numbers in every field. The only way to catch it is to read the
 * values off the serial console and compare them against the JK phone app, so
 * the first frame of every session is dumped in full, cell by cell.
 *
 * Afterwards a one-line summary at a slow cadence: the poll runs at 1 Hz and
 * logging every frame would swamp the console and the log ring in PSRAM. */
#define LOG_SUMMARY_INTERVAL_MS  5000

static void log_snapshot(const bms_snapshot_t *s, bool full)
{
    const int  ma   = (int)s->pack_current_ma;
    const char sign = (ma < 0) ? '-' : '+';
    const int  aa   = (ma < 0) ? -ma : ma;

    ESP_LOGI(TAG,
             "pack %d.%03d V  %c%d.%03d A  soc %u.%u%%  cells %u  %u..%u mV "
             "delta %u  mos %d.%d C  chg=%d dsg=%d bal=%d  rssi %d dBm",
             (int)s->pack_mv / 1000, (int)s->pack_mv % 1000,
             sign, aa / 1000, aa % 1000,
             s->soc_permille / 10, s->soc_permille % 10,
             s->cell_count, s->cell_min_mv, s->cell_max_mv, s->cell_delta_mv,
             s->mos_temp_deci_c / 10, abs(s->mos_temp_deci_c % 10),
             s->chg_mos_on, s->dsg_mos_on, s->balancing, s->rssi_dbm);
    if (full && (s->valid_mask & BMS_V_IDENTITY)) {
        ESP_LOGI(TAG, "  peer '%s' hw=%s sw=%s layout=%uS",
                 s->peer_name[0] ? s->peer_name : "?", s->hw_version,
                 s->sw_version, s->cell_layout);
    }

    /* Counters alongside the values. Without them a quiet link and a link
     * dropping every frame on CRC look identical from the console, and those
     * two need opposite fixes. */
    const bms_diag_t *d = bms_model_diag();
    ESP_LOGI(TAG, "  frames=%u crc_err=%u len_err=%u dropped=%u "
                  "timeouts=%u reconnects=%u",
             (unsigned)d->frames_ok, (unsigned)d->crc_errors,
             (unsigned)d->length_errors, (unsigned)d->dropped_fragments,
             (unsigned)d->timeouts, (unsigned)d->reconnects);

    if (!full) return;

    ESP_LOGI(TAG, "  soh %u.%u%%  cycles %u  remain %d mAh of %d  "
                  "cycle-cap %d mAh  bal-current %d mA  heater %d/%d mA",
             s->soh_permille / 10, s->soh_permille % 10,
             (unsigned)s->cycle_count, (int)s->remaining_mah,
             (int)s->nominal_mah, (int)s->cycle_capacity_mah,
             (int)s->balance_current_ma, s->heater_on,
             (int)s->heater_current_ma);
    ESP_LOGI(TAG, "  temps %d.%d / %d.%d C  valid_mask 0x%05x  seq %u",
             s->temp_deci_c[0] / 10, abs(s->temp_deci_c[0] % 10),
             s->temp_deci_c[1] / 10, abs(s->temp_deci_c[1] % 10),
             (unsigned)s->valid_mask, (unsigned)s->sample_seq);
    if (s->valid_mask & BMS_V_SLEEP_TIMER) {
        ESP_LOGI(TAG, "  sleep timer %u s", (unsigned)s->sleep_timer_s);
    }
    if (s->valid_mask & BMS_V_TIMERS) {
        ESP_LOGI(TAG, "  emergency timer %u s", s->emergency_timer_s);
    }

    /* Cell-by-cell is what actually proves the layout. A 32-byte offset error
     * shifts every one of these and nothing else looks wrong. */
    for (unsigned i = 0; i < s->cell_count && i < BMS_MAX_CELLS; i++) {
        if (!(s->cell_valid_mask & (1u << i))) continue;
        if (s->wire_res_valid_mask & (1u << i)) {
            ESP_LOGI(TAG, "  cell %02u  %u mV  wire %u mOhm",
                     i + 1, s->cell_mv[i], s->wire_res_mohm[i]);
        } else {
            ESP_LOGI(TAG, "  cell %02u  %u mV", i + 1, s->cell_mv[i]);
        }
    }
}

/* --------------------------------------------------------------- rx worker */

static void bms_worker(void *arg)
{
    (void)arg;
    uint8_t         chunk[128];
    bms_snapshot_t  snap;
    int64_t         last_rx_us    = esp_timer_get_time();
    int64_t         last_log_us   = 0;
    int64_t         last_rearm_us = 0;

    /* Per-session, reset on every new subscription. */
    bool            session       = false;
    uint32_t        seen_gen      = s_link_gen;
    int64_t         last_probe_us = 0;
    bool            info_answered = false;
    bool            kicked        = false;

    for (;;) {
        /* NVS on behalf of bind()/unbind(), which run on the LVGL task. */
        if (s_store_peer) { s_store_peer = false; peer_store(&s_bound_addr); }
        if (s_erase_peer) { s_erase_peer = false; peer_store(NULL); }

        const size_t got = xStreamBufferReceive(s_rx, chunk, sizeof chunk,
                                                pdMS_TO_TICKS(200));
        const int64_t now = esp_timer_get_time();

        for (size_t off = 0; off < got; ) {
            const size_t take = got - off;
            size_t used = 0;
            const jk_feed_result_t r = jk_feed(&s_jk, chunk + off, take, &snap,
                                               &used);

            switch (r) {
            case JK_FEED_SNAPSHOT:
                /* The parser knows nothing about the radio, so the transport
                 * stamps link quality on the way past. */
                snap.rssi_dbm = s_rssi_dbm;
                if (s_peer_name[0]) {
                    snprintf(snap.peer_name, sizeof snap.peer_name, "%s",
                             s_peer_name);
                }
                bms_model_publish(&snap);
                bms_model_diag_bump(BMS_DIAG_FRAME_OK);
                bms_model_set_link_state(BMS_LINK_LIVE);
                last_rx_us = now;
                if (!s_logged_full) {
                    s_logged_full = true;
                    log_snapshot(&snap, true);
                    last_log_us = now;
                } else if (now - last_log_us >=
                           (int64_t)LOG_SUMMARY_INTERVAL_MS * 1000) {
                    last_log_us = now;
                    log_snapshot(&snap, false);
                }
                break;
            case JK_FEED_SETTINGS:
                last_rx_us = now;
                break;

            case JK_FEED_DEVICE_INFO:
                last_rx_us    = now;
                /* Answered. Whether or not we can use the layout, there is
                 * nothing left to ask -- see the probe block below. */
                info_answered = true;
                /* First proof the bound address is really a JK we can read:
                 * it answered device-info AND we recognise its layout. Only
                 * now is it worth surviving a reboot — persisting at bind
                 * time meant a mistyped tap on a neighbour's pack came back
                 * on every boot. */
                if (!s_peer_persisted && s_bound &&
                    jk_get_proto(&s_jk) != JK_PROTO_UNKNOWN) {
                    s_peer_persisted = true;
                    s_store_peer     = true;
                }
                if (jk_get_proto(&s_jk) == JK_PROTO_UNKNOWN) {
                    ESP_LOGW(TAG, "unrecognised JK hw '%s' — cannot pick layout",
                             s_jk.hw_version);
                    bms_model_set_link_state(BMS_LINK_UNSUPPORTED);
                } else {
                    ESP_LOGI(TAG, "JK hw=%s sw=%s layout=%s", s_jk.hw_version,
                             s_jk.sw_version,
                             jk_get_proto(&s_jk) == JK_PROTO_02_32S ? "32S" : "24S");
                }
                break;
            case JK_FEED_CRC_ERROR:
                bms_model_diag_bump(BMS_DIAG_CRC_ERROR);
                break;
            default:
                break;
            }
            /* Resume where the parser stopped. It returns at the end of each
             * completed frame, so the rest of the slice still has to be fed:
             * chunks are 128 bytes against 300-byte frames, so a frame ends
             * mid-chunk constantly, and skipping the remainder threw away the
             * next frame's preamble -- roughly every other reading. The
             * advance is forced to be non-zero so a parser bug cannot spin
             * this loop forever. */
            off += used ? used : take;
        }

        /* Re-arm a connect that was refused before it could produce an event.
         *
         * arm_connect() is otherwise only reached from connect-failed,
         * disconnect, BLE sync and bind. When ble_gap_connect() returns an
         * error the first three never fire again: s_connecting drops back to
         * false with no connection to later disconnect, so the link sits in
         * CONNECTING for good and only re-pairing by hand recovers it. The
         * reachable case is boot -- a peer restored from NVS arms on sync
         * while another central procedure still owns the initiator.
         *
         * arm_connect() re-checks bound/synced/connecting/connected itself, so
         * this is a no-op whenever the link is healthy or already in flight.
         * s_scanning is checked here and not there because the user's own scan
         * owns the initiator on purpose: NimBLE would refuse the connect, and
         * re-arming through a pairing sweep would only log a warning every
         * five seconds while the rider is reading the device list.
         * The reads below are not synchronised for the same reason the
         * s_subscribed read below is not: the blocking receive above is an
         * opaque call, so the compiler cannot hoist them out of the loop. */
        if (s_bound && s_conn == BLE_HS_CONN_HANDLE_NONE && !s_connecting &&
            !s_scanning &&
            now - last_rearm_us >= (int64_t)RECONNECT_RETRY_MS * 1000) {
            last_rearm_us = now;
            ESP_LOGI(TAG, "re-arming connect");
            arm_connect();
        }

        if (!s_subscribed) continue;

        /* New subscription: start the session clock here rather than at boot.
         * The GAP callback has just sent device-info as part of writing the
         * CCCD, so the retry below must be measured from now -- timing it from
         * last_rx_us meant the worker saw a several-second-old timestamp on
         * its very next tick and fired a second device-info immediately, one
         * extra beep on every single connect. */
        if (!session || seen_gen != s_link_gen) {
            session       = true;
            seen_gen      = s_link_gen;
            last_probe_us = now;
            info_answered = false;
            kicked        = false;
        }

        /* Nothing streams until the pack has said which byte layout it speaks.
         * Retry only while it has not answered at all: a pack that answers
         * with a version we do not recognise is not going to answer any
         * differently the next time, and re-asking every three seconds would
         * beep at the rider forever over a fault no amount of asking fixes.
         * The tab already reads UNSUPPORTED in that case. */
        if (jk_get_proto(&s_jk) == JK_PROTO_UNKNOWN) {
            if (!info_answered &&
                now - last_probe_us >= (int64_t)PROBE_RETRY_MS * 1000) {
                last_probe_us = now;
                bms_model_diag_bump(BMS_DIAG_TIMEOUT);
                send_cmd(JK_CMD_DEVICE_INFO);
            }
            continue;
        }

        /* Layout known. Kick the stream exactly once per session, then go
         * quiet -- every command from here on is an audible beep at the pack. */
        if (!kicked) {
            kicked = true;
            ESP_LOGI(TAG, "layout known — starting the stream (no polling "
                          "from here; the pack beeps at every command)");
            send_cmd(JK_CMD_CELL_INFO);
            last_rx_us = now;
            continue;
        }

        /* Watchdog only. Re-kick rather than tearing the link down: a
         * reconnect costs far more than one command, and JK occasionally goes
         * quiet under load. Deliberately not gated on s_active -- the pack
         * streams whether or not anyone is looking at the tab, and letting the
         * stream die while the tab is closed would show stale data the moment
         * it reopens. */
        if (now - last_rx_us >= (int64_t)STREAM_SILENCE_MS * 1000) {
            last_rx_us = now;
            bms_model_diag_bump(BMS_DIAG_TIMEOUT);
            ESP_LOGW(TAG, "no frames for %d ms — re-kicking the stream",
                     STREAM_SILENCE_MS);
            send_cmd(JK_CMD_CELL_INFO);
        }
    }
}

/* --------------------------------------------------------------- public API */

void ble_bms_client_init(void)
{
    if (s_inited) return;

    jk_init(&s_jk);
    s_rx = xStreamBufferCreate(RX_STREAM_BYTES, 1);
    if (!s_rx) {
        ESP_LOGE(TAG, "stream buffer alloc failed — BMS disabled");
        return;
    }
    if (xTaskCreate(bms_worker, "bms_rx", 4096, NULL, 4, &s_worker) != pdPASS) {
        ESP_LOGE(TAG, "worker task alloc failed — BMS disabled");
        vStreamBufferDelete(s_rx);
        s_rx = NULL;
        return;
    }

    s_inited = true;

    /* Restore the previously paired pack so the tab is live after a reboot
     * without the rider re-pairing. arm_connect() is a no-op until the BLE
     * stack syncs, which happens later. */
    if (peer_load(&s_bound_addr)) {
        s_bound = true;
        bms_model_set_link_state(BMS_LINK_CONNECTING);
        ESP_LOGI(TAG, "restored bound peer from NVS");
    } else {
        bms_model_set_link_state(BMS_LINK_UNBOUND);
    }
    ESP_LOGI(TAG, "initialised");
}

void ble_bms_on_ble_sync(uint8_t own_addr_type)
{
    s_own_addr_type = own_addr_type;
    s_synced        = true;
    arm_connect();
}

void ble_bms_set_scan_cb(ble_bms_scan_cb_t cb) { s_scan_cb = cb; }

bool ble_bms_scan_is_active(void) { return s_scanning; }

bool ble_bms_scan_start(void)
{
    if (!s_inited || !s_synced) return false;
    if (s_scanning) return true;          /* already running is not a failure */
    /* NimBLE allows one initiator at a time and a pending connect blocks the
     * scanner. Say so rather than failing silently: the UI showed an empty
     * device list forever with no way to tell "none nearby" from "we never
     * looked". */
    if (s_connecting) {
        ESP_LOGW(TAG, "scan refused: a connect is already in flight");
        return false;
    }

    struct ble_gap_disc_params dp = { 0 };
    dp.passive          = 0;   /* active: JK puts its name in the scan rsp */
    dp.filter_duplicates = 1;

    const int rc = ble_gap_disc(s_own_addr_type, SCAN_DURATION_MS, &dp,
                                bms_gap_event, NULL);
    if (rc == 0) {
        s_scanning = true;
        bms_model_set_link_state(BMS_LINK_SCANNING);
        ESP_LOGI(TAG, "scanning for JK BMS (%d ms)", SCAN_DURATION_MS);
        return true;
    }
    ESP_LOGW(TAG, "ble_gap_disc rc=%d", rc);
    return false;
}

void ble_bms_scan_stop(void)
{
    if (s_scanning) {
        ble_gap_disc_cancel();
        s_scanning = false;
    }
}

void ble_bms_bind(const ble_addr_t *addr)
{
    ble_bms_bind_named(addr, NULL, 0);
}

void ble_bms_bind_named(const ble_addr_t *addr, const char *name, int8_t rssi)
{
    ble_bms_scan_stop();

    if (!addr) {
        s_bound = false;
        memset(&s_bound_addr, 0, sizeof s_bound_addr);
        if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        reset_link_state();
        bms_model_reset();
        s_rssi_dbm = 0;
        s_peer_name[0] = '\0';
        s_erase_peer = true;
        ESP_LOGI(TAG, "unbound");
        return;
    }

    /* Rebinding while a link is up must tear the old one down, or the new
     * address only takes effect after whatever random disconnect comes next —
     * and until then the tab shows the OLD pack under the NEW name. */
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
        reset_link_state();
    }

    s_bound_addr    = *addr;
    s_bound         = true;
    s_peer_persisted = false;

    /* Identity only ever arrives in advertising, and bind() has just stopped
     * the scan -- so without taking it from the caller here, a freshly bound
     * pack has no name until some later sweep happens to see it again, and
     * the tab meanwhile shows the PREVIOUS pack's name over the new one's
     * numbers. Clearing first means an unnamed bind shows nothing rather than
     * something wrong. */
    s_peer_name[0] = '\0';
    s_rssi_dbm     = 0;
    if (name && name[0]) {
        snprintf(s_peer_name, sizeof s_peer_name, "%s", name);
        s_rssi_dbm = rssi;
    }
    /* NOT persisted yet: see the device-info branch in the worker. A peer
     * that never answers must not survive a reboot. */
    /* A previous pack's numbers must not linger under a new binding. */
    bms_model_reset();
    ESP_LOGI(TAG, "bound to %02x:%02x:%02x:%02x:%02x:%02x",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
    arm_connect();
}

bool ble_bms_get_bound(ble_addr_t *out)
{
    if (s_bound && out) *out = s_bound_addr;
    return s_bound;
}

void ble_bms_set_active(bool active)
{
    if (s_active == active) return;
    s_active = active;
    /* This no longer changes what the radio does. The pack streams on its own
     * once kicked and the only way to stop it is a command, which beeps -- so
     * pausing would cost the very noise the streaming design exists to avoid,
     * twice. The link and the stream stay up; the tab simply stops being
     * looked at, and reopening it shows a frame that is at most a second old
     * instead of waiting for a fresh poll. */
    ESP_LOGI(TAG, "tab %s (stream keeps running either way)",
             active ? "shown" : "hidden");
    /* Re-dump on the next frame: the values will have moved on and the full
     * dump is what the bring-up session reads. */
    if (active) s_logged_full = false;
}

bool ble_bms_is_connected(void)
{
    return s_conn != BLE_HS_CONN_HANDLE_NONE && s_subscribed;
}
