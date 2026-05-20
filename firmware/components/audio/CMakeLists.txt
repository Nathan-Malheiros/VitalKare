idf_component_register(
    SRCS
        "audio_capture.c"
        "audio_encoder.c"
        "adpcm.c"
        "recording_recovery.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_i2s
        heap
        log
        freertos
        events
        network
        core
        storage
        nvs_flash
)
