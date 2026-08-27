idf_component_register(
    SRCS "esp32_s3_rlcd_gb_emulator.c"
    INCLUDE_DIRS "."
    EMBED_FILES "font16.bin"
    REQUIRES
        board_sdmmc
        board_battery
        board_clock
        board_shtc3
        board_speaker
        esp_driver_gpio
        esp_timer
        board_rlcd
        rlcd_test_pattern
        board_lvgl
        board_audio
        audio_wave_ui
        gb_emu
        gbc_emu
        web_gamepad
        bt_gamepad
        wifi_time_sync
        audio_player
)
