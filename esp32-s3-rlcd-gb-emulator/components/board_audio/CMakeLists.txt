idf_component_register(
    SRCS "board_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_i2c
        esp_driver_i2s
        esp_codec_dev
)
