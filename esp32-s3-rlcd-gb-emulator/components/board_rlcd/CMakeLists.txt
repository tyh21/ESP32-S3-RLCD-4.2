idf_component_register(
    SRCS "board_rlcd.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        driver
        esp_lcd
        heap
)
