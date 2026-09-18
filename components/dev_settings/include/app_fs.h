/*
 * Shared backup filesystem: a SPIFFS mount of the (otherwise unused) 16.9 MB
 * "storage" partition at /vescfs. Owned here (in the low-level dev_settings
 * component) so both vesc_config (config backups) and vesc_trip_persist (trip
 * totals) can use it without a circular dependency.
 *
 * Mounted lazily on a background task — the first-ever mount formats the
 * partition, which is slow (~1 min); callers must tolerate app_fs_ready()==false
 * until it completes and only fopen() under /vescfs once ready.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    APP_FS_NONE = 0,
    APP_FS_MOUNTING,
    APP_FS_FORMATTING,   /* first-time format in progress — show on screen */
    APP_FS_READY,
    APP_FS_FAIL,
};

/* Idempotent: kicks the background mount/format on first call. */
void        app_fs_ensure(void);
int         app_fs_state(void);    /* APP_FS_* */
bool        app_fs_ready(void);
const char *app_fs_base(void);     /* "/vescfs" */

#ifdef __cplusplus
}
#endif
