idf_component_register(
    SRCS "board_lvgl.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board_rlcd
        esp_timer
        lvgl__lvgl
)
