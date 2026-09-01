#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

/* If main/bench_wifi.h is present (gitignored), force STA mode on those
 * creds regardless of Kconfig role. Lets a dev join an existing network
 * for protocol testing without rebuilding sdkconfig. */
#if __has_include("bench_wifi.h")
#include "bench_wifi.h"
#define WIFI_BENCH_OVERRIDE 1
#else
#define WIFI_BENCH_OVERRIDE 0
#endif

#define WIFI_USE_STA (WIFI_BENCH_OVERRIDE || CONFIG_WIFI_ROLE_STA)
#define WIFI_USE_AP  (!WIFI_BENCH_OVERRIDE && CONFIG_WIFI_ROLE_AP)

static const char *TAG = "wifi";

#define WIFI_READY_BIT BIT0
#define WIFI_FAIL_BIT  BIT1

static EventGroupHandle_t s_wifi_events;
static wifi_ap_info_t s_ap_info;

#if WIFI_USE_STA
static int s_retry_count;
#endif

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
#if WIFI_USE_STA
    /* Retry budget: bench override gets unlimited (we want to keep trying
     * until the dev's home AP is in range), Kconfig STA mode honours its limit. */
#if WIFI_BENCH_OVERRIDE
    const int max_retry = INT32_MAX;
#else
    const int max_retry = CONFIG_WIFI_STA_MAX_RETRY;
#endif
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < max_retry) {
            s_retry_count++;
            ESP_LOGW(TAG, "disconnected, retry %d", s_retry_count);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "give up after %d retries", s_retry_count);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_READY_BIT);
    }
#elif WIFI_USE_AP
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP \"%s\" up on ch %u", s_ap_info.ssid, (unsigned)s_ap_info.channel);
        xEventGroupSetBits(s_wifi_events, WIFI_READY_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "client " MACSTR " joined", MAC2STR(e->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "client " MACSTR " left", MAC2STR(e->mac));
    }
#endif
}

#if WIFI_USE_AP
static esp_err_t start_ap(void)
{
    esp_netif_create_default_wifi_ap();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    snprintf(s_ap_info.ssid, sizeof(s_ap_info.ssid), "%s-%02X%02X",
             CONFIG_WIFI_AP_SSID_PREFIX, mac[4], mac[5]);
    strlcpy(s_ap_info.password, CONFIG_WIFI_AP_PASSWORD, sizeof(s_ap_info.password));
    snprintf(s_ap_info.bssid_str, sizeof(s_ap_info.bssid_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    s_ap_info.channel = CONFIG_WIFI_AP_CHANNEL;

    wifi_config_t cfg = {
        .ap = {
            .channel = CONFIG_WIFI_AP_CHANNEL,
            /* AA Wireless only ever pairs with one phone, but bench testing
             * is much friendlier with a few extra slots for laptops. */
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg.required = false,
        },
    };
    strlcpy((char *)cfg.ap.ssid, s_ap_info.ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(s_ap_info.ssid);
    strlcpy((char *)cfg.ap.password, s_ap_info.password, sizeof(cfg.ap.password));
    if (strlen(CONFIG_WIFI_AP_PASSWORD) == 0) {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}
#endif

#if WIFI_USE_STA
#if WIFI_BENCH_OVERRIDE
#define STA_SSID     BENCH_WIFI_SSID
#define STA_PASSWORD BENCH_WIFI_PASSWORD
#else
#define STA_SSID     CONFIG_WIFI_STA_SSID
#define STA_PASSWORD CONFIG_WIFI_STA_PASSWORD
#endif

static esp_err_t start_sta(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, STA_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, STA_PASSWORD, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

#if WIFI_BENCH_OVERRIDE
    ESP_LOGW(TAG, "BENCH override: joining \"%s\" (bench_wifi.h present)", STA_SSID);
#else
    ESP_LOGI(TAG, "connecting to \"%s\"", STA_SSID);
#endif
    return ESP_OK;
}
#endif

esp_err_t wifi_manager_start(void)
{
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Not ESP_ERROR_CHECK. On this board the Wi-Fi MAC lives on the C6 and is
     * reached over ESP-Hosted, so esp_wifi_init fails outright whenever that
     * link is down -- a miswired or unresponsive co-processor, or a host built
     * for the wrong transport. Aborting there panics the P4 into a reboot loop
     * that no amount of power-cycling escapes, and takes the dashboard with
     * it: the VESC telemetry this device exists for arrives over CAN and needs
     * nothing from the C6. Fail the call, let the caller log it, keep driving. */
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    const esp_err_t werr = esp_wifi_init(&init_cfg);
    if (werr != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s — Wi-Fi unavailable (co-processor "
                      "link down). Android Auto and OTA over Wi-Fi are off; "
                      "the dashboard is unaffected.", esp_err_to_name(werr));
        vEventGroupDelete(s_wifi_events);
        s_wifi_events = NULL;
        return werr;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                       &on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                                       &on_event, NULL, NULL));

#if WIFI_USE_AP
    return start_ap();
#elif WIFI_USE_STA
    return start_sta();
#else
#error "AA_WIFI_ROLE not selected"
#endif
}

esp_err_t wifi_manager_wait_ready(uint32_t timeout_ms)
{
    /* start() releases the group when it bails out early, and the caller is
     * allowed to ask anyway. */
    if (!s_wifi_events) return ESP_FAIL;

    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_READY_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, ticks);
    if (bits & WIFI_READY_BIT) {
        return ESP_OK;
    }
    if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

const wifi_ap_info_t *wifi_manager_get_ap_info(void)
{
#if WIFI_USE_AP
    return &s_ap_info;
#else
    return NULL;
#endif
}
