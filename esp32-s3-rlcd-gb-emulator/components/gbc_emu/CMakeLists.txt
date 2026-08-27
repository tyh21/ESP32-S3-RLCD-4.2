idf_component_register(
    SRCS
        "gbc_emu.c"
        "third_party/gnuboy/cpu.c"
        "third_party/gnuboy/gnuboy.c"
        "third_party/gnuboy/hw.c"
        "third_party/gnuboy/lcd.c"
        "third_party/gnuboy/sound.c"
    INCLUDE_DIRS
        "include"
        "third_party/gnuboy"
    REQUIRES
        board_rlcd
        board_speaker
        esp_timer
        heap
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -Wno-error=unused-function
    -Wno-error=unused-variable
    -Wno-error=format
    -Wno-error=sign-compare
    -Wno-error=unterminated-string-initialization
)
