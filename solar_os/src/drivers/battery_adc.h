#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int raw;
    uint16_t adc_mv;
    uint16_t battery_mv;
    bool calibrated;
} battery_adc_sample_t;

esp_err_t battery_adc_init(void);
esp_err_t battery_adc_read(battery_adc_sample_t *sample);
