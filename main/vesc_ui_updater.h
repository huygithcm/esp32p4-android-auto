#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Spawns a 20 Hz task that pulls vesc_rt_data values and pushes them
 * into the GUI Guider widgets via update_speed()/update_current()/...
 * Takes bsp_display_lock around each batch of widget updates. */
esp_err_t vesc_ui_updater_start(void);

#ifdef __cplusplus
}
#endif
