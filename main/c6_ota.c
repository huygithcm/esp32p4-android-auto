#include "c6_ota.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "esp_hosted_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "ota_partition.h"
#include "ota_screen.h"

static const char *TAG = "c6_ota";

/* Attempt counter, persisted across the restart this module triggers.
 *
 * A successful OTA returns C6_OTA_STATUS_UPDATED and app_main restarts the P4
 * to resync. Nothing bounded that: any condition leaving the slave version
 * unreadable or still mismatched after a flash -- a wedged SDIO link, an image
 * the C6 accepts but does not boot -- became an update/restart/update loop
 * with the update screen permanently in front of the user and no way out but
 * USB. The dashboard is this device's primary job and does not need the C6 at
 * all, so after a couple of rounds it is strictly better to give up, say so,
 * and boot.
 *
 * Cleared as soon as a boot finds the slave already matching, so a genuine
 * one-shot update on a fresh board costs nothing. */
#define C6_OTA_NVS_NS    "c6_ota"
#define C6_OTA_NVS_TRIES "tries"
#define C6_OTA_MAX_TRIES 2

static uint8_t tries_load(void)
{
    nvs_handle_t h;
    uint8_t n = 0;
    if (nvs_open(C6_OTA_NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    nvs_get_u8(h, C6_OTA_NVS_TRIES, &n);
    nvs_close(h);
    return n;
}

static void tries_store(uint8_t n)
{
    nvs_handle_t h;
    if (nvs_open(C6_OTA_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (n) nvs_set_u8(h, C6_OTA_NVS_TRIES, n);
    else   nvs_erase_key(h, C6_OTA_NVS_TRIES);
    nvs_commit(h);
    nvs_close(h);
}

/* Last successfully-read slave version, formatted as "MAJ.MIN.PAT". Cleared
 * if the read fails on the current boot so the UI doesn't show stale data
 * from a prior session. */
static char s_slave_ver_str[16] = "";

const char *c6_ota_get_slave_version_str(void) { return s_slave_ver_str; }

static void on_progress(ota_partition_stage_t stage, uint32_t done, uint32_t total, void *ctx)
{
    (void)ctx;
    static int last_logged_pct = -1;

    switch (stage) {
    case OTA_PARTITION_STAGE_VALIDATE:
        ESP_LOGI(TAG, "validating image");
        ota_screen_set_status("Validating image...");
        last_logged_pct = -1;
        break;
    case OTA_PARTITION_STAGE_BEGIN:
        ESP_LOGI(TAG, "ota_begin (%u bytes)", (unsigned)total);
        ota_screen_set_status("Erasing C6 flash...");
        ota_screen_set_progress(0, total ? total : 1);
        break;
    case OTA_PARTITION_STAGE_WRITE: {
        int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
        if (pct >= last_logged_pct + 5 || pct == 100) {
            ESP_LOGI(TAG, "writing… %d%% (%u/%u)", pct, (unsigned)done, (unsigned)total);
            last_logged_pct = pct;
        }
        ota_screen_set_status("Writing firmware...");
        ota_screen_set_progress(done, total);
        break;
    }
    case OTA_PARTITION_STAGE_END:
        ESP_LOGI(TAG, "ota_end — verifying");
        ota_screen_set_status("Verifying...");
        ota_screen_set_progress(total, total);
        break;
    case OTA_PARTITION_STAGE_DONE:
        ESP_LOGI(TAG, "ota done — activating");
        ota_screen_set_status("Activating new firmware...");
        break;
    }
}

static bool slave_supports_activate(const esp_hosted_coprocessor_fwver_t *v)
{
    return (v->major1 > 2) || (v->major1 == 2 && v->minor1 > 5);
}

c6_ota_status_t c6_ota_check_and_update(void)
{
    if (esp_hosted_init() != ESP_OK) {
        ESP_LOGW(TAG, "esp_hosted_init failed (probably already initialized)");
    }
    esp_err_t conn = esp_hosted_connect_to_slave();
    if (conn != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_connect_to_slave: %s", esp_err_to_name(conn));
        return C6_OTA_STATUS_FAILED;
    }

    esp_hosted_coprocessor_fwver_t slave_ver = { 0 };
    esp_err_t vret = esp_hosted_get_coprocessor_fwversion(&slave_ver);
    if (vret == ESP_OK) {
        ESP_LOGI(TAG, "Slave version: %" PRIu32 ".%" PRIu32 ".%" PRIu32,
                 slave_ver.major1, slave_ver.minor1, slave_ver.patch1);
        snprintf(s_slave_ver_str, sizeof(s_slave_ver_str),
                 "%" PRIu32 ".%" PRIu32 ".%" PRIu32,
                 slave_ver.major1, slave_ver.minor1, slave_ver.patch1);
    } else {
        ESP_LOGW(TAG, "fwversion not readable: %s — assuming update needed",
                 esp_err_to_name(vret));
        s_slave_ver_str[0] = '\0';
    }

    uint32_t host_ver = ESP_HOSTED_VERSION_VAL(ESP_HOSTED_VERSION_MAJOR_1,
                                              ESP_HOSTED_VERSION_MINOR_1,
                                              ESP_HOSTED_VERSION_PATCH_1);
    uint32_t slave = ESP_HOSTED_VERSION_VAL(slave_ver.major1, slave_ver.minor1, slave_ver.patch1);
    if (vret == ESP_OK && (host_ver & 0xFFFFFF00) == (slave & 0xFFFFFF00)) {
#if CONFIG_C6_OTA_FORCE
        ESP_LOGW(TAG, "Slave matches but C6_OTA_FORCE=y — running anyway");
        /* The version is fine, so the give-up counter below must not build up
         * and eventually disable the very thing FORCE asks for. */
        tries_store(0);
#else
        ESP_LOGI(TAG, "Slave already matches host major.minor — skipping OTA");
        tries_store(0);
        return C6_OTA_STATUS_NOT_REQUIRED;
#endif
    }

    /* Give up rather than loop. Reaching here on consecutive boots means the
     * previous flash did not change what the slave reports, so repeating it
     * will not either -- and each round costs a restart with the update
     * screen up, which is what the user actually sees. */
    const uint8_t tries = tries_load();
    if (tries >= C6_OTA_MAX_TRIES) {
        ESP_LOGE(TAG, "Slave still mismatched after %u attempts — giving up "
                      "and booting with the firmware it has. Wi-Fi and BLE may "
                      "not work; the dashboard does not need them.", tries);
        return C6_OTA_STATUS_NOT_REQUIRED;
    }
    tries_store((uint8_t)(tries + 1));

    ESP_LOGW(TAG, "Slave needs update — running OTA (attempt %u of %u)",
             tries + 1, C6_OTA_MAX_TRIES);
    ota_screen_show("Don't power off");
    ota_screen_set_status("Preparing...");

    esp_err_t r = ota_partition_perform_with_cb(NULL, on_progress, NULL);
    if (r != ESP_HOSTED_SLAVE_OTA_COMPLETED) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(r));
        ota_screen_set_status("Update failed");
        return C6_OTA_STATUS_FAILED;
    }

    if (slave_supports_activate(&slave_ver)) {
        if (esp_hosted_slave_ota_activate() != ESP_OK) {
            ESP_LOGW(TAG, "ota_activate failed (continuing anyway)");
        }
    } else {
        ESP_LOGI(TAG, "Slave < 2.6 — activate API not used; new fw boots after restart");
    }

    ota_screen_set_status("Done — restarting...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    return C6_OTA_STATUS_UPDATED;
}
