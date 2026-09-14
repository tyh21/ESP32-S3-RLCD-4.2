#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int pin;
    bool runtime_allowed;
    bool adc_capable;
    int unit;
    int channel;
} solar_os_adc_pin_info_t;

typedef struct {
    int pin;
    int raw;
    uint16_t voltage_mv;
    int unit;
    int channel;
    bool calibrated;
} solar_os_adc_sample_t;

esp_err_t solar_os_adc_init(void);
size_t solar_os_adc_pin_count(void);
bool solar_os_adc_get_pin_info(size_t index, solar_os_adc_pin_info_t *info);
esp_err_t solar_os_adc_read(int pin, solar_os_adc_sample_t *sample);
