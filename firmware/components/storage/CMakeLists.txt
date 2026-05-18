idf_component_register(
    SRCS
        "sd_manager.c"
        "log_writer.c"
        "offline_cache.c"
    INCLUDE_DIRS "."
    REQUIRES
        fatfs
        sdmmc
        log
        freertos
        esp_timer
        driver
        esp_driver_sdmmc
)
