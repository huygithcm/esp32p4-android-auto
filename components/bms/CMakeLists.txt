idf_component_register(
    SRCS "bms_model.c"
         "bms_jk.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer
)
