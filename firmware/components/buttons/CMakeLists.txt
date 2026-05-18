idf_component_register(
    SRCS "button_handler.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        driver
        log
        freertos
        events
)
