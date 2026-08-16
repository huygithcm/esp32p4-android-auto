#pragma once

#include <stdbool.h>

#include "lvgl.h"

/* Build the BMS frontend inside a Realtime tab. Only one instance is active. */
void bms_view_create(lv_obj_t *parent);
void bms_view_set_active(bool active);
void bms_view_destroy(void);

