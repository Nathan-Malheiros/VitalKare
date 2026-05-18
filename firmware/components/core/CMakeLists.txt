idf_component_register(
    SRCS "core.c"
    INCLUDE_DIRS "."
    REQUIRES
        log
        freertos
        nvs_flash
        esp_timer
)
