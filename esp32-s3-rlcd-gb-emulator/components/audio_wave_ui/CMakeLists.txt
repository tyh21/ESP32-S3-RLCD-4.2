idf_component_register(
    SRCS "audio_wave_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board_lvgl
        board_audio
)
