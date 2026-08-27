idf_component_register(
    SRCS "board_sdmmc.c"
    INCLUDE_DIRS "include"
    REQUIRES
        fatfs
        esp_driver_sdmmc
)
