idf_component_register(
    SRCS "sim_sensors.c"
    INCLUDE_DIRS "."
    REQUIRES
        core
        events
        network
        esp_timer
        log
        freertos
)
