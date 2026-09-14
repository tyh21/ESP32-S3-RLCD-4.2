#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define PWM_PORT_FREQ_MIN_HZ 1U
#define PWM_PORT_FREQ_MAX_HZ 40000U
#define PWM_PORT_DUTY_MAX_PERCENT 100U

typedef struct {
    int pin;
    bool active;
    ledc_channel_t channel;
    uint32_t freq_hz;
    uint8_t duty_percent;
} pwm_port_status_t;

esp_err_t pwm_port_init(void);
esp_err_t pwm_port_set(gpio_num_t pin, uint32_t freq_hz, uint8_t duty_percent);
esp_err_t pwm_port_stop(gpio_num_t pin);
bool pwm_port_get(gpio_num_t pin, pwm_port_status_t *status);
