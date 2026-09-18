# Auto-generated firmware-version tables live under generated/ and are produced
# by tools/gen_vesc_config.py. Glob them so adding a version doesn't require
# editing this file.
file(GLOB VESC_CONFIG_GENERATED_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.c")

idf_component_register(
    SRCS
        "vesc_config_serdes.c"
        "vesc_config_transport.c"
        "vesc_config_runtime.c"
        ${VESC_CONFIG_GENERATED_SRCS}
    INCLUDE_DIRS
        "include"
    REQUIRES
        vesc_can
        dev_settings
    PRIV_REQUIRES
        esp_timer
        freertos
        spiffs
)
