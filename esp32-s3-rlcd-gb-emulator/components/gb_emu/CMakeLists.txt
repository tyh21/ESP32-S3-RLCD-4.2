idf_component_register(
    SRCS "gb_emu.c"
    INCLUDE_DIRS
        "include"
        "third_party"
    REQUIRES
        board_rlcd
        board_speaker
        esp_timer
        freertos
        heap
        log
)
