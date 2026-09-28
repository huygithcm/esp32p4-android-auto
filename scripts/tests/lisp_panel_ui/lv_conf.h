#ifndef LV_CONF_H
#define LV_CONF_H

/* Headless host allocation/tick driver; input, widgets and animation are real
 * managed LVGL. Match device geometry, RGB565 and 10 ms read/repaint cadence. */
#define LV_COLOR_DEPTH 16
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc
#define LV_TICK_CUSTOM 0
#define LV_DISP_DEF_REFR_PERIOD 10
#define LV_INDEV_DEF_READ_PERIOD 10
#define LV_DPI_DEF 130
#define LV_FONT_MONTSERRAT_24 1
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_OBJ 1
#define LV_USE_ASSERT_MEM_INTEGRITY 1
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER abort();
#define LV_USE_LOG 0
#define LV_USE_FLEX 1

#endif
