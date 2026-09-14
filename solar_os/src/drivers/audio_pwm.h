#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#define AUDIO_PWM_SAMPLE_RATE 16000U
#define AUDIO_PWM_CARRIER_HZ 78125U
#define AUDIO_PWM_FRAMES_PER_BLOCK 256U

esp_err_t audio_pwm_open(gpio_num_t pin);
esp_err_t audio_pwm_write_s16(const int16_t *samples,
                              size_t frames,
                              uint8_t channels,
                              uint8_t volume);
void audio_pwm_close(void);
