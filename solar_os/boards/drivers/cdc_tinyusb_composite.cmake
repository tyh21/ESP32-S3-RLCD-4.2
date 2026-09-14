set(SOLAR_OS_BOARD_CDC_DRIVER "tinyusb_composite")
list(APPEND SOLAR_OS_BOARD_SRCS
    "drivers/cdc_port_tinyusb.c"
)
# ESP-IDF evaluates component requirements once before PlatformIO's board
# selection is available. In that pass an ESP32 build can temporarily see the
# default S3 profile, even though the final configure uses its real board.
# Publish the target-specific managed components only for USB-OTG SoCs.
if(IDF_TARGET MATCHES "^esp32(s2|s3|p4)$")
    list(APPEND SOLAR_OS_BOARD_REQUIRES
        esp_tinyusb
        tinyusb
    )
endif()
