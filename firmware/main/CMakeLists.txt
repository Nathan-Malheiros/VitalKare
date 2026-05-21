idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." ".." "../components"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        log
        freertos

        core
        events
        network
        http_client
        buttons
        sensors
        display
        storage
        audio
        camera
        watchdog
        i2c_bus
        simulation
)
