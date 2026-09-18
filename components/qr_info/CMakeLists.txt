idf_component_register(
    SRCS "qr_info.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_wifi
)
# Legacy identifier is retained only as the existing QR helper's macro API.
# No Android Auto discovery or protocol code is included in this branch.
target_compile_definitions(${COMPONENT_LIB} PRIVATE AA_MDNS_HOSTNAME="vesc-display")
