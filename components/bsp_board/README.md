# `bsp_board` — the board support contract

This component is a **placeholder**. The firmware in `main/` and the dashboard
in `Super_VESC_Display/` talk to the hardware only through the small API below;
supply one implementation per board and nothing else in the tree has to change.

The ESP32-P4 original this was extracted from is kept verbatim at
[`reference/bsp_p4/`](../../reference/bsp_p4/) — read it as the worked example,
not as something to compile (it is MIPI-DSI + PPA and targets `esp32p4` only).

## What the firmware actually calls

Counted across `main/`, `components/` and `Super_VESC_Display/custom/` — this
is the whole surface, nothing else in the BSP header is used:

| Symbol | Call sites | Notes |
|---|---|---|
| `bsp_display_lock(uint32_t timeout_ms)` / `bsp_display_unlock(void)` | 27 each | LVGL mutex. Every UI mutation outside the LVGL task is wrapped in these. |
| `bsp_display_start_with_config(bsp_display_cfg_t *cfg)` | 4 | Brings up panel + LVGL, returns `lv_display_t *`. |
| `bsp_display_brightness_init/set/get` | 8 / 11 / 2 | Backlight PWM, percent. |
| `bsp_display_backlight_on/off` | 5 / 4 | |
| `bsp_display_get_panel_handle()` | 4 | `esp_lcd_panel_handle_t`. |
| `bsp_display_get_touch_handle()` | 4 | `esp_lcd_touch_handle_t`, used by `main/touch_input.c`. |
| `bsp_display_get_input_dev()` | 3 | The adapter's auto-installed LVGL indev, so `main.c` can unregister it. |
| `bsp_sdcard_mount/unmount` | 6 / 2 | Optional — `files_screen` / `files_http` / `ble_files` degrade gracefully. |
| `bsp_i2c_init()` / `bsp_i2c_get_handle()` | 5 / 2 | Shared bus for the touch controller. |
| `BSP_SD_MOUNT_POINT` | many | String macro, e.g. `"/sdcard"`. |

`bsp_display_cfg_t` in the P4 BSP is:

```c
typedef struct {
    esp_lv_adapter_config_t          lv_adapter_cfg;
    esp_lv_adapter_rotation_t        rotation;
    esp_lv_adapter_tear_avoid_mode_t tear_avoid_mode;
    struct { unsigned swap_xy, mirror_x, mirror_y; } touch_flags;
} bsp_display_cfg_t;
```

Keep the shape. `main/display_init.c` fills `lv_adapter_cfg`, `rotation` and
`tear_avoid_mode`, and pins the LVGL task to core 0.

## Three things that change versus the P4

1. **No rotation.** The P4 panel is a 480×800 portrait DSI panel turned 90°, and
   that rotation is threaded through the display, the touch mapping and the
   overlays. An 800×480 RGB panel on S3 is natively landscape, so
   `rotation` should be `ESP_LV_ADAPTER_ROTATE_NONE` and the coordinate
   swapping in `main/touch_input.c` can come out. Do both together — half a
   rotation is worse than either.
2. **No PPA, no DMA2D.** LVGL renders in software. Do not enable a draw
   accelerator path; do put the LVGL draw buffer in *internal* SRAM and let the
   flush copy into the PSRAM framebuffer. See §4 of
   [`docs/ESP32_S3_PORT_PLAN.md`](../../docs/ESP32_S3_PORT_PLAN.md).
3. **Bounce buffers.** With the framebuffer in PSRAM, `esp_lcd_rgb_panel` needs
   `bounce_buffer_size_px` set or the panel tears under load.

## Current state

`idf_component_register(INCLUDE_DIRS "include")` with no sources and no
headers. `main/` will fail to compile at `#include "bsp/esp-bsp.h"` until you
add `include/bsp/esp-bsp.h` and an implementation — that is the intended
starting point of this branch, see [`PORT_README.md`](../../PORT_README.md).
