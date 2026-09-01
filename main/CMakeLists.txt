idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "display_init.c"
        "idle_screen.c"
        "ota_screen.c"
        "ota_http.c"
        "files_http.c"
        "lisp_http.c"
        "ble_host.c"
        "ble_bms_client.c"
        "ble_cadence_client.c"
        "ble_nus.c"
        "notif_bridge.c"
        "pas.c"
        "ble_ota.c"
        "ble_files.c"
        "notif_toast.c"
        "fonts/aabridge_font_24.c"
        "fonts/aabridge_font_32.c"
        "fonts/aabridge_font_38.c"
        "fonts/aabridge_font_48.c"
        "touch_input.c"
        "vesc_ui_updater.c"
        "vesc_sim.c"
        "files_screen.c"
        # ── Not compiled yet: both depend on ESP32-P4-only peripherals ──
        # splash_screen.c    — driver/ppa.h to pre-rotate the GIF frames, plus
        #                      driver/jpeg_decode.h. The S3 panels are natively
        #                      landscape, so the rotation can simply be dropped.
        # debug_uart_bridge.c— driver/jpeg_encode.h (P4 hardware JPEG encoder).
        #                      S3 has none: either fall back to the raw RGB565
        #                      screenshot path this file already has, or link a
        #                      software encoder.
        # music_info_view.c  — driver/jpeg_decode.h (P4 hardware JPEG decoder)
        #                      for the album art the phone pushes over BLE.
        #                      CONFIG_ESP_LVGL_ADAPTER_ENABLE_DECODER is already
        #                      on and gives LVGL a software JPEG path; route
        #                      decode_art_jpeg() through it. Until then main.c's
        #                      dashboard-theme hook has nothing to attach.
        # "splash_screen.c"
        # "debug_uart_bridge.c"
        # "music_info_view.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_event
        esp_netif
        esp_wifi
        esp_hw_support
        hal
        bt
        esp_timer
        esp_partition
        esp_ringbuf
        esp_lcd
        esp_mm
        lwip
        mbedtls
        driver
        console
        app_update
        esp_http_server
        # bsp_board: per-board display / touch / SD support. Provide one
        # implementation of the contract in components/bsp_board/README.md;
        # reference/bsp_p4/ is the ESP32-P4 original, kept for reference only.
        bsp_board
        espressif__esp_lvgl_adapter
        vesc_ui
        vesc_can
        vesc_config
        bms
        dev_settings
        log_capture
        trip_log
        joltwallet__littlefs
        json
)

# Web LISP editor page (main/web/lisp_editor.html) — gzipped at build time and
# embedded in .rodata, served with Content-Encoding: gzip by lisp_http.c. Kept
# as a real .html file instead of a C string literal (the way ota_http.c and
# files_http.c hold their much smaller pages) so it stays editable; gzip takes
# ~50 KiB of markup down to ~15 KiB of flash.
#
# target_add_binary_data rather than EMBED_FILES in the registration above:
# EMBED_FILES needs the file to exist at configure time, and this one is
# generated. add_dependencies makes the gzip run before the object is built.
set(_lisp_web_src "${CMAKE_CURRENT_LIST_DIR}/web/lisp_editor.html")
set(_lisp_web_gz  "${CMAKE_CURRENT_BINARY_DIR}/lisp_editor.html.gz")
# (scripts/compress_gz.py rather than `gzip -9 -n > out`: add_custom_command
#  runs the command directly, without a shell to interpret the redirection —
#  and Windows has no gzip at all. The script reproduces `-n` exactly. ${_py} is
#  the IDF venv interpreter; a bare `python3`/`python` is missing on one host OS
#  or the other.)
idf_build_get_property(_py PYTHON)
add_custom_command(
    OUTPUT "${_lisp_web_gz}"
    COMMAND "${_py}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/compress_gz.py"
            "${_lisp_web_src}" "${_lisp_web_gz}"
    DEPENDS "${_lisp_web_src}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/compress_gz.py"
    COMMENT "Compressing web LISP editor -> lisp_editor.html.gz"
    VERBATIM)
add_custom_target(lisp_web_gz DEPENDS "${_lisp_web_gz}")
add_dependencies(${COMPONENT_LIB} lisp_web_gz)
target_add_binary_data(${COMPONENT_LIB} "${_lisp_web_gz}" BINARY)
