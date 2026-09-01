cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_vesc_display)

# main/lv_conf.h is the single source of truth for LVGL configuration.
# It must reach every component that does '#include "lv_conf.h"' or
# '#include "lvgl.h"' (BSP, esp_lv_adapter, vesc_ui, Super_VESC_Display
# generated/custom files). Putting main/ on the global include path is
# the simplest way to satisfy that. Pair this with CONFIG_LV_CONF_SKIP=n
# in sdkconfig so the LVGL package picks up our header instead of
# generating its own from Kconfig.
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_SOURCE_DIR}/main" APPEND)

# reference/ holds the ESP32-P4 originals (BSP, sdkconfig overlays, partition
# tables) kept for porting reference only. It must stay OUT of the component
# search path so the component manager never tries to build the P4 BSP.
# EXTRA_COMPONENT_DIRS is not set, and reference/ is not components/, so this
# is a no-op today — the comment is here so nobody "fixes" it by adding it.

# Auto-tag the firmware artifact with PROJECT_VER (read from version.txt).
add_custom_command(
    TARGET gen_project_binary POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
            "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}-v${PROJECT_VER}.bin"
    COMMENT "Tagged: ${CMAKE_PROJECT_NAME}-v${PROJECT_VER}.bin"
    VERBATIM)
