idf_component_register(
    SRCS "camera_driver.c"
    INCLUDE_DIRS "."
    REQUIRES
        espressif__esp32-camera
        log
        freertos
        http_client
        heap
)
