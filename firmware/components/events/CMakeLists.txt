idf_component_register(
    SRCS "event_dispatcher.c"
    INCLUDE_DIRS "."
    REQUIRES
        log
        freertos
        esp_timer
)
