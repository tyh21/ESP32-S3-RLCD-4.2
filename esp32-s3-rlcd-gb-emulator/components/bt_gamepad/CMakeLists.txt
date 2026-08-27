idf_component_register(
    SRCS "bt_gamepad.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_hid
        bt
        nvs_flash
        esp_timer
        log
)
