/* See app_fs.h. Background mount/format of the "storage" partition as LittleFS
 * (real directories — needed for the /trips/<id>/ trip history). */
#include "app_fs.h"

#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_fs";

#define APP_FS_BASE   "/vescfs"
#define APP_FS_LABEL  "storage"

static volatile int s_state = APP_FS_NONE;

const char *app_fs_base(void) { return APP_FS_BASE; }
int  app_fs_state(void)       { return s_state; }
bool app_fs_ready(void)       { return s_state == APP_FS_READY; }

static void fs_task(void *arg)
{
    (void)arg;
    esp_vfs_littlefs_conf_t conf = {
        .base_path = APP_FS_BASE,
        .partition_label = APP_FS_LABEL,
        .format_if_mount_failed = false,
    };
    esp_err_t e = esp_vfs_littlefs_register(&conf);   /* fast path: already formatted */
    if (e != ESP_OK) {
        /* First use (or migrating away from the old SPIFFS layout): format. */
        ESP_LOGW(TAG, "not a LittleFS volume — formatting (one-time)");
        s_state = APP_FS_FORMATTING;
        conf.format_if_mount_failed = true;
        e = esp_vfs_littlefs_register(&conf);
    }
    if (e == ESP_OK) {
        s_state = APP_FS_READY;
        ESP_LOGI(TAG, "LittleFS mounted at %s", APP_FS_BASE);
    } else {
        s_state = APP_FS_FAIL;
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(e));
    }
    vTaskDelete(NULL);
}

void app_fs_ensure(void)
{
    if (s_state != APP_FS_NONE) return;
    s_state = APP_FS_MOUNTING;     /* claim before spawning */
    if (xTaskCreate(fs_task, "app_fs", 4096, NULL, 2, NULL) != pdPASS) {
        s_state = APP_FS_FAIL;
    }
}
