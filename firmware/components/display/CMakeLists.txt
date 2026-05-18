idf_component_register(
    SRCS
        "oled_driver.c"
        "ui_renderer.c"
    INCLUDE_DIRS "."
    REQUIRES
        i2c_bus
        esp_driver_i2c
        log
        freertos
        core
        esp_timer
)
