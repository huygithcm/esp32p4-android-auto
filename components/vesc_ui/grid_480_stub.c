/* Stub for the _grid_480_800x480 background image.
 *
 * Super_VESC_Display/generated/setup_scr_dashboard.c sets this as the
 * dashboard's bg_image (LV_IMG_DECLARE(_grid_480_800x480)) but then
 * lv_obj_set_style_bg_img_opa(..., 0, ...) makes it fully transparent —
 * it's never drawn. The original asset .c is 27 MB of source / ~1.15 MB
 * of binary in flash, so we exclude it from the embedded build (see
 * components/vesc_ui/CMakeLists.txt) and provide this 1×1 placeholder
 * to satisfy the linker. */

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif
#ifndef LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_CONST
#endif

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t _grid_480_800x480_map[] = {
    0x00, 0x00, 0x00, /* 1×1 transparent pixel (TRUE_COLOR_ALPHA) */
};

const lv_img_dsc_t _grid_480_800x480 = {
    .header.always_zero = 0,
    .header.w = 1,
    .header.h = 1,
    .data_size = sizeof(_grid_480_800x480_map),
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .data = _grid_480_800x480_map,
};
