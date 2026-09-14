#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_PWM_FREQ_MIN_HZ 1U
#define SOLAR_OS_PWM_FREQ_MAX_HZ 40000U
#define SOLAR_OS_PWM_DUTY_MAX_PERCENT 100U

typedef struct {
    int pin;
    bool runtime_allowed;
    bool active;
    int channel;
    uint32_t freq_hz;
    uint8_t duty_percent;
} solar_os_pwm_pin_info_t;

esp_err_t solar_os_pwm_init(void);
size_t solar_os_pwm_pin_count(void);
bool solar_os_pwm_get_pin_info(size_t index, solar_os_pwm_pin_info_t *info);
esp_err_t solar_os_pwm_set(int pin, uint32_t freq_hz, uint8_t duty_percent);
esp_err_t solar_os_pwm_stop(int pin);
