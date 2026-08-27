idf_component_register(
    SRCS "wifi_time_sync.c"
    INCLUDE_DIRS "include"
    REQUIRES
        log
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        nvs_flash
        lwip
        board_clock
)