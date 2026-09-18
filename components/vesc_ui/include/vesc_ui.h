#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the VESC dashboard offscreen and run custom_init(). Idempotent. */
void vesc_ui_init(void);

/* Returns the dashboard screen object, or NULL if vesc_ui_init() hasn't
 * been called yet. Pass to lv_scr_load() to make it active. */
lv_obj_t *vesc_ui_get_screen(void);

#ifdef __cplusplus
}
#endif
