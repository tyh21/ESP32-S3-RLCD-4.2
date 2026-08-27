idf_component_register(
    SRCS "web_gamepad.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_event
        esp_http_server
        esp_netif
        esp_wifi
        board_battery
        board_clock
        gb_emu
        gbc_emu
        log
        nvs_flash
)
