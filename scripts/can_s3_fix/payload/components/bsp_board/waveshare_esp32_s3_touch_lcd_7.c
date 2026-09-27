/* Board support package — Waveshare ESP32-S3-Touch-LCD-7.
 *
 * Ported from the ESP32-P4 BSP kept at reference/bsp_p4/. Same API (see
 * README.md), different silicon underneath:
 *
 *   - MIPI-DSI + ST7701 panel  ->  16-bit parallel RGB, no controller IC.
 *   - LEDC PWM backlight       ->  PWM inside the I2C IO expander.
 *   - SDMMC 4-bit slot 0       ->  SD over SPI, chip select on that expander.
 *   - Panel rotated 90 degrees ->  native landscape, no rotation at all.
 */

#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "bsp_err_check.h"

static const char *TAG = "WS_S3_LCD7";

sdmmc_card_t *bsp_sdcard = NULL;

static bool                     i2c_initialized;
static i2c_master_bus_handle_t  i2c_handle;
static i2c_master_dev_handle_t  ioext_dev;        /* the expander at 0x24 */
/* Keep native USB selected while bringing up LCD/touch. CAN is selected
 * explicitly by bsp_can_mux_enable() only when physical CAN starts. */
static uint8_t                  ioext_shadow = (uint8_t)(0xFF & ~BSP_EXIO_USB_SEL);

static esp_lcd_panel_handle_t   panel_handle;
static esp_lcd_touch_handle_t   tp;
static lv_indev_t              *disp_indev;
static int                      brightness;

/* Framebuffer count the RGB peripheral must allocate. Derived from the caller's
 * tear-avoidance mode in bsp_display_start_with_config() — the panel has to be
 * created with the right count before the LVGL adapter ever sees it. */
static uint8_t                  s_num_fbs = 2;

/**************************************************************************************************
 * I2C
 **************************************************************************************************/

esp_err_t bsp_i2c_init(void)
{
    if (i2c_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .i2c_port = BSP_I2C_NUM,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_conf, &i2c_handle),
                        TAG, "i2c_new_master_bus(port %d, sda %d, scl %d) failed",
                        BSP_I2C_NUM, BSP_I2C_SDA, BSP_I2C_SCL);

    i2c_initialized = true;

    /* One-shot bus scan. Cheap, and this board puts three different things on
     * one bus behind an expander whose two addresses are write-only — when
     * something does not answer, the first question is always "what IS on the
     * bus". */
    {
        char found[96];
        int  n = 0;
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            if (i2c_master_probe(i2c_handle, addr, 50) == ESP_OK && n < (int)sizeof(found) - 6) {
                n += snprintf(found + n, sizeof(found) - n, " 0x%02x", addr);
            }
        }
        ESP_LOGI(TAG, "I2C scan (sda %d, scl %d):%s", BSP_I2C_SDA, BSP_I2C_SCL,
                 n ? found : " nothing responded");
    }
    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    BSP_ERROR_CHECK_RETURN_ERR(i2c_del_master_bus(i2c_handle));
    i2c_initialized = false;
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    return i2c_handle;
}

static esp_err_t bsp_i2c_device_probe(uint8_t addr)
{
    return i2c_master_probe(i2c_handle, addr, 100);
}

/**************************************************************************************************
 * IO expander
 *
 * Waveshare's own "IO extension" part, not a CH422G: one I2C device at 0x24
 * with registers, every access two bytes { register, value }. The 800x480
 * sibling board really does carry a CH422G with its several write-only
 * addresses — that difference is why an image built for that board leaves
 * this one's backlight, panel reset, SD chip select and USB/CAN mux entirely
 * undriven, with no I2C error to show for it.
 *
 * The output latch can only be written whole and there is no read-back of it,
 * so ioext_shadow is the record. It starts at 0xFF: that is what the chip
 * itself powers up with (all lines high), so the first bsp_exio_set() does not
 * disturb lines it was not asked about.
 **************************************************************************************************/

static esp_err_t ioext_write(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(ioext_dev, buf, sizeof(buf), 1000);
}

static esp_err_t ioext_init(void)
{
    if (ioext_dev) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_IOEXT_ADDR,
        .scl_speed_hz    = CONFIG_BSP_I2C_CLK_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_handle, &dev_cfg, &ioext_dev),
                        TAG, "IO expander: add device 0x%02x failed", BSP_IOEXT_ADDR);

    /* Set the latch before enabling outputs, including USB_SEL=0. */
    ESP_RETURN_ON_ERROR(ioext_write(BSP_IOEXT_REG_OUTPUT, ioext_shadow),
                        TAG, "IO expander: initial output write failed");

    /* All eight lines to output. */
    ESP_RETURN_ON_ERROR(ioext_write(BSP_IOEXT_REG_MODE, 0xFF),
                        TAG, "IO expander: not responding at 0x%02x", BSP_IOEXT_ADDR);
    return ESP_OK;
}

esp_err_t bsp_exio_set(uint8_t mask, bool level)
{
    ESP_RETURN_ON_ERROR(ioext_init(), TAG, "IO expander init failed");

    uint8_t next = level ? (uint8_t)(ioext_shadow | mask)
                         : (uint8_t)(ioext_shadow & ~mask);
    ESP_RETURN_ON_ERROR(ioext_write(BSP_IOEXT_REG_OUTPUT, next),
                        TAG, "IO expander: output write 0x%02x failed", next);
    ioext_shadow = next;
    return ESP_OK;
}

esp_err_t bsp_backlight_pwm_set(int percent)
{
    ESP_RETURN_ON_ERROR(ioext_init(), TAG, "IO expander init failed");

    if (percent < 0)   percent = 0;
    /* Waveshare's driver clamps at 97 %: the expander's PWM never reaches full
     * scale cleanly and 98-100 % comes out as a dark or flickering panel. */
    if (percent > 97)  percent = 97;

    const uint8_t duty = (uint8_t)((percent * 255) / 100);
    return ioext_write(BSP_IOEXT_REG_PWM, duty);
}

esp_err_t bsp_can_mux_enable(void)
{
    ESP_LOGI(TAG, "routing GPIO%d/%d to the CAN transceiver (USB port goes away)",
             BSP_CAN_TX, BSP_CAN_RX);
    return bsp_exio_set(BSP_EXIO_USB_SEL, true);
}

/**************************************************************************************************
 * uSD card
 **************************************************************************************************/

esp_err_t bsp_sdcard_mount(void)
{
    /* Idempotent: the on-device file browser and the BLE file manager both call
     * this freely, and a second spi_bus_initialize() would fail with
     * ESP_ERR_INVALID_STATE — indistinguishable, from the caller's side, from
     * "no card". Once bsp_sdcard is set the card is up; just say so. */
    if (bsp_sdcard) {
        return ESP_OK;
    }

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
        .max_files = 5,
        .allocation_unit_size = 64 * 1024
    };

    /* Chip select hangs off the IO expander, so the SPI driver cannot toggle it
     * per transaction (gpio_cs = -1 below). The card is alone on this bus, so
     * hold CS asserted (low) for as long as it is mounted — this is what
     * Waveshare's own SD example does. */
    BSP_ERROR_CHECK_RETURN_ERR(bsp_exio_set(BSP_EXIO_SD_CS, false));

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num     = BSP_SD_MOSI,
        .miso_io_num     = BSP_SD_MISO,
        .sclk_io_num     = BSP_SD_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t ret = spi_bus_initialize(BSP_SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SD: spi_bus_initialize failed (%s)", esp_err_to_name(ret));
        bsp_exio_set(BSP_EXIO_SD_CS, true);
        return ret;
    }
    const bool bus_was_ours = (ret == ESP_OK);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BSP_SD_SPI_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_NC;
    slot_config.host_id = BSP_SD_SPI_HOST;

    ret = esp_vfs_fat_sdspi_mount(BSP_SD_MOUNT_POINT, &host, &slot_config,
                                  &mount_config, &bsp_sdcard);
    if (ret != ESP_OK) {
        /* Release the bus and de-assert CS so a later retry (card inserted after
         * a first no-card attempt) starts from a clean slate. */
        bsp_sdcard = NULL;
        if (bus_was_ours) {
            spi_bus_free(BSP_SD_SPI_HOST);
        }
        bsp_exio_set(BSP_EXIO_SD_CS, true);
    }
    return ret;
}

esp_err_t bsp_sdcard_unmount(void)
{
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, bsp_sdcard);
    bsp_sdcard = NULL;
    spi_bus_free(BSP_SD_SPI_HOST);
    bsp_exio_set(BSP_EXIO_SD_CS, true);
    return ret;
}

/**************************************************************************************************
 * Backlight
 *
 * Enable line on expander IO2, and the expander generates the PWM itself
 * (register 0x05) — so brightness really is continuous here, no LEDC channel
 * and no backlight GPIO involved.
 **************************************************************************************************/

esp_err_t bsp_display_brightness_init(void)
{
    return ioext_init();
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    } else if (brightness_percent < 0) {
        brightness_percent = 0;
    }
    brightness = brightness_percent;

    /* Two things to drive: the enable line and the expander's own PWM. Setting
     * the duty alone leaves a disabled backlight dark; setting the line alone
     * leaves the last duty in force. */
    ESP_RETURN_ON_ERROR(bsp_exio_set(BSP_EXIO_LCD_BL, brightness_percent > 0),
                        TAG, "backlight enable failed");
    return bsp_backlight_pwm_set(brightness_percent);
}

int bsp_display_brightness_get(void)
{
    return brightness;
}

esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(100);
}

/**************************************************************************************************
 * Panel
 **************************************************************************************************/

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    (void)config;
    ESP_RETURN_ON_FALSE(ret_panel, ESP_ERR_INVALID_ARG, TAG, "ret_panel is NULL");
    if (ret_io) {
        *ret_io = NULL;   /* parallel RGB has no control channel */
    }

    /* Release the panel's reset line before the RGB peripheral starts clocking
     * pixels at it. */
    ESP_RETURN_ON_ERROR(bsp_exio_set(BSP_EXIO_LCD_RST, false), TAG, "panel reset assert failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(bsp_exio_set(BSP_EXIO_LCD_RST, true), TAG, "panel reset release failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    const esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz           = CONFIG_BSP_LCD_PIXEL_CLOCK_HZ,
            .h_res             = BSP_LCD_H_RES,
            .v_res             = BSP_LCD_V_RES,
            /* Timing verbatim from Waveshare's 13_lvgl_v8_demo for this panel
             * (research/_sources/waveshare_s3_lcd7b/.../rgb_lcd_port.c). The
             * horizontal blanking is huge — 162+152+48 against 1024 visible —
             * and that is not slack to trim: with the 800x480 board's tiny
             * porches this glass shows a narrow strip of picture and black
             * everywhere else, which is exactly the symptom that led here. */
            .hsync_pulse_width = 162,
            .hsync_back_porch  = 152,
            .hsync_front_porch = 48,
            .vsync_pulse_width = 45,
            .vsync_back_porch  = 13,
            .vsync_front_porch = 3,
            .flags.pclk_active_neg = 1,
        },
        .data_width  = 16,
        .bits_per_pixel = 16,
        .num_fbs     = s_num_fbs,
        /* Framebuffers live in PSRAM (768 KB each); the RGB peripheral cannot
         * be fed from PSRAM directly without tearing, so DMA refills a small
         * internal-SRAM bounce buffer line by line. */
        .bounce_buffer_size_px = BSP_LCD_H_RES * CONFIG_BSP_LCD_RGB_BOUNCE_LINES,
        .sram_trans_align  = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = BSP_LCD_RGB_HSYNC,
        .vsync_gpio_num = BSP_LCD_RGB_VSYNC,
        .de_gpio_num    = BSP_LCD_RGB_DE,
        .pclk_gpio_num  = BSP_LCD_RGB_PCLK,
        .disp_gpio_num  = BSP_LCD_RGB_DISP,
        .data_gpio_nums = {
            BSP_LCD_RGB_DATA0,  BSP_LCD_RGB_DATA1,  BSP_LCD_RGB_DATA2,  BSP_LCD_RGB_DATA3,
            BSP_LCD_RGB_DATA4,  BSP_LCD_RGB_DATA5,  BSP_LCD_RGB_DATA6,  BSP_LCD_RGB_DATA7,
            BSP_LCD_RGB_DATA8,  BSP_LCD_RGB_DATA9,  BSP_LCD_RGB_DATA10, BSP_LCD_RGB_DATA11,
            BSP_LCD_RGB_DATA12, BSP_LCD_RGB_DATA13, BSP_LCD_RGB_DATA14, BSP_LCD_RGB_DATA15,
        },
        .flags.fb_in_psram = 1,
    };

    ESP_LOGI(TAG, "Install RGB LCD panel driver (%d fbs, %d MHz pclk)",
             (int)panel_config.num_fbs, (int)(CONFIG_BSP_LCD_PIXEL_CLOCK_HZ / 1000000));
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, ret_panel), TAG,
                        "esp_lcd_new_rgb_panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*ret_panel), TAG, "esp_lcd_panel_init failed");
    return ESP_OK;
}

/**************************************************************************************************
 * Touch
 **************************************************************************************************/

esp_err_t bsp_touch_new(const bsp_display_cfg_t *cfg, esp_lcd_touch_handle_t *ret_touch)
{
    assert(cfg != NULL);

    BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());

    /* GT911 reset, with INT held low so the controller latches address 0x5D.
     * Reset is on the IO expander, INT is a real GPIO. Timings from
     * Waveshare's own bring-up. */
    const gpio_config_t int_conf = {
        .pin_bit_mask = 1ULL << BSP_LCD_TOUCH_INT,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    BSP_ERROR_CHECK_RETURN_ERR(gpio_config(&int_conf));

    BSP_ERROR_CHECK_RETURN_ERR(bsp_exio_set(BSP_EXIO_TOUCH_RST, false));
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(BSP_LCD_TOUCH_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    BSP_ERROR_CHECK_RETURN_ERR(bsp_exio_set(BSP_EXIO_TOUCH_RST, true));
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Hand the pin back as an input so it floats to the controller's idle level
     * instead of being driven low forever. We poll the controller rather than
     * using the interrupt (main/touch_input.c), so it is not passed on. */
    const gpio_config_t int_release = {
        .pin_bit_mask = 1ULL << BSP_LCD_TOUCH_INT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    BSP_ERROR_CHECK_RETURN_ERR(gpio_config(&int_release));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,   /* on the IO expander, done above */
        .int_gpio_num = GPIO_NUM_NC,   /* polled, not interrupt-driven */
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = cfg->touch_flags.swap_xy,
            .mirror_x = cfg->touch_flags.mirror_x,
            .mirror_y = cfg->touch_flags.mirror_y,
        },
    };

    esp_lcd_panel_io_i2c_config_t tp_io_config;
    if (ESP_OK == bsp_i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS)) {
        ESP_LOGI(TAG, "Touch 0x5d found");
        esp_lcd_panel_io_i2c_config_t config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        memcpy(&tp_io_config, &config, sizeof(config));
    } else if (ESP_OK == bsp_i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP)) {
        ESP_LOGI(TAG, "Touch 0x14 found");
        esp_lcd_panel_io_i2c_config_t config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        memcpy(&tp_io_config, &config, sizeof(config));
    } else {
        ESP_LOGE(TAG, "Touch not found");
        return ESP_ERR_NOT_FOUND;
    }
    tp_io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_handle, &tp_io_config, &tp_io_handle), TAG, "");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, ret_touch), TAG,
                        "GT911 init failed");

    /* Ask the touch controller what size the glass is. The GT911 config block
     * holds the panel's native resolution (0x8048/0x804A, little-endian), put
     * there by whoever built the module — so it is the one on-board source of
     * truth for what this panel actually is, independent of any datasheet.
     * Log it and warn when it disagrees with what the RGB timing drives. */
    {
        i2c_device_config_t probe_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = tp_io_config.dev_addr,
            .scl_speed_hz    = CONFIG_BSP_I2C_CLK_SPEED_HZ,
        };
        i2c_master_dev_handle_t probe = NULL;
        if (i2c_master_bus_add_device(i2c_handle, &probe_cfg, &probe) == ESP_OK) {
            const uint8_t reg[2] = { 0x80, 0x48 };   /* X_max low byte */
            uint8_t v[4] = {0};
            if (i2c_master_transmit_receive(probe, reg, sizeof(reg), v, sizeof(v), 1000) == ESP_OK) {
                unsigned gx = (unsigned)v[0] | ((unsigned)v[1] << 8);
                unsigned gy = (unsigned)v[2] | ((unsigned)v[3] << 8);
                ESP_LOGI(TAG, "GT911 reports panel %ux%u", gx, gy);
                if (gx && gy && (gx != BSP_LCD_H_RES || gy != BSP_LCD_V_RES)) {
                    ESP_LOGW(TAG, "panel is %ux%u but the RGB timing drives %dx%d "
                                  "— fix BSP_LCD_H_RES/V_RES and the porches",
                             gx, gy, BSP_LCD_H_RES, BSP_LCD_V_RES);
                }
            }
            i2c_master_bus_rm_device(probe);
        }
    }
    return ESP_OK;
}

/**************************************************************************************************
 * Display bring-up
 **************************************************************************************************/

static lv_display_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);
    esp_err_t err = bsp_display_new(NULL, &panel_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel bring-up failed: %s", esp_err_to_name(err));
        return NULL;
    }

    ESP_LOGD(TAG, "Add LCD screen");
    esp_lv_adapter_display_config_t disp_cfg = {
        .panel = panel_handle,
        .panel_io = NULL,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_RGB,
            .rotation = cfg->rotation,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            /* Draw buffer in INTERNAL SRAM: the S3 has no 2D accelerator, so
             * LVGL renders in software, and letting it render straight into
             * PSRAM is the single biggest render cost (docs/ESP32_S3_PORT_PLAN.md
             * §4). Internal RAM is the scarce one here, and this competes
             * directly with the BLE stack: 50 lines (1024x50x2 = 100 KB) fails
             * to allocate outright, and 20 lines starved ble_bms of its worker
             * task. 10 lines = 20 KB, which leaves the radios their room and
             * still renders a small dirty rect in ~5 ms. Waveshare's own demo
             * puts these buffers in PSRAM instead; that frees internal RAM at
             * the cost of render speed, and is the fallback if this ever gets
             * tighter again. */
            .buffer_height = 10,
            .use_psram = false,
            /* No PPA on S3 — the P4-only draw accelerator. */
            .enable_ppa_accel = false,
            .require_double_buffer = false,
        },
        .tear_avoid_mode = cfg->tear_avoid_mode,
        .te_sync = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
    };

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "esp_lv_adapter_register_display failed (rotation %d, tear mode %d, %d fbs)",
                 (int)cfg->rotation, (int)cfg->tear_avoid_mode, (int)s_num_fbs);
    }
    return disp;
}

static lv_indev_t *bsp_display_indev_init(const bsp_display_cfg_t *cfg, lv_display_t *disp)
{
    assert(cfg != NULL);

    /* GT911 cold-boot is timing-sensitive: the first I2C config read can fail
     * if the chip is still latching its address from the INT pin. Retry. */
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        tp = NULL;
        err = bsp_touch_new(cfg, &tp);
        if (err == ESP_OK && tp) {
            break;
        }
        ESP_LOGW(TAG, "GT911 init attempt %d/3 failed (%s), retrying",
                 attempt, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (err != ESP_OK || !tp) {
        ESP_LOGE(TAG, "GT911 init gave up after 3 attempts");
        return NULL;
    }

    const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, tp);
    return esp_lv_adapter_register_touch(&touch_cfg);
}

lv_display_t *bsp_display_start(void)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0
        }
    };
    return bsp_display_start_with_config(&cfg);
}

lv_display_t *bsp_display_start_with_config(bsp_display_cfg_t *cfg)
{
    lv_display_t *disp;

    assert(cfg != NULL);
    esp_err_t err = esp_lv_adapter_init(&cfg->lv_adapter_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lv_adapter_init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    /* How many framebuffers the RGB peripheral has to allocate is decided by
     * the tear-avoidance mode the caller asked for; the panel is created inside
     * bsp_display_lcd_init(), so stash it where bsp_display_new() can read it. */
    s_num_fbs = esp_lv_adapter_get_required_frame_buffer_count(cfg->tear_avoid_mode, cfg->rotation);

    err = bsp_display_brightness_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight/IO-expander init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    disp = bsp_display_lcd_init(cfg);
    if (!disp) {
        return NULL;
    }

    /* GT911 sometimes fails its first I2C transaction on cold boot —
     * keep the display alive without touch instead of failing the whole BSP. */
    disp_indev = bsp_display_indev_init(cfg, disp);
    if (!disp_indev) {
        ESP_LOGW(TAG, "Touch init failed — display will run without touch");
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    return disp;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return disp_indev;
}

esp_err_t bsp_display_lock(uint32_t timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    esp_lv_adapter_unlock();
}

esp_lcd_panel_handle_t bsp_display_get_panel_handle(void)
{
    return panel_handle;
}

esp_lcd_touch_handle_t bsp_display_get_touch_handle(void)
{
    return tp;
}
