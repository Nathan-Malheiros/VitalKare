idf_component_register(
    SRCS "vitalkare_http.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_client
        log
        freertos
        core
        events
)
