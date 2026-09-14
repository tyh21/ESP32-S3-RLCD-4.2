#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_expansion.h"

#define SOLAR_OS_NEOPIXEL_MAX_PIXELS 256U

typedef struct {
    char name[SOLAR_OS_EXPANSION_DEVICE_NAME_MAX];
    int data_pin;
    size_t pixel_count;
} solar_os_neopixel_info_t;

esp_err_t solar_os_neopixel_attach(const char *name,
                                   const solar_os_expansion_binding_t *bindings,
                                   size_t binding_count);
esp_err_t solar_os_neopixel_detach(const char *name);

size_t solar_os_neopixel_count(void);
bool solar_os_neopixel_get(size_t index, solar_os_neopixel_info_t *info);
esp_err_t solar_os_neopixel_set(const char *name,
                               size_t index,
                               uint8_t red,
                               uint8_t green,
                               uint8_t blue);
esp_err_t solar_os_neopixel_fill(const char *name,
                                uint8_t red,
                                uint8_t green,
                                uint8_t blue);
esp_err_t solar_os_neopixel_show(const char *name);
esp_err_t solar_os_neopixel_clear(const char *name);
