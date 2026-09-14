#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "hal/adc_types.h"

typedef struct {
    int pin;
    int raw;
    uint16_t voltage_mv;
    adc_unit_t unit;
    adc_channel_t channel;
    bool calibrated;
} adc_port_sample_t;

esp_err_t adc_port_init(void);
bool adc_port_is_adc_capable(gpio_num_t pin, adc_unit_t *unit, adc_channel_t *channel);
esp_err_t adc_port_configure_pin(gpio_num_t pin, adc_atten_t atten, adc_bitwidth_t bitwidth);
esp_err_t adc_port_read(gpio_num_t pin, adc_port_sample_t *sample);
