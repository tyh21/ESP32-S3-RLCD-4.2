idf_component_register(
    SRCS "rlcd_test_pattern.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board_rlcd
        esp_timer
)
