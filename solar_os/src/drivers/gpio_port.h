#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef enum {
    GPIO_PORT_MODE_INPUT,
    GPIO_PORT_MODE_OUTPUT,
} gpio_port_mode_t;

typedef enum {
    GPIO_PORT_PULL_NONE,
    GPIO_PORT_PULL_UP,
    GPIO_PORT_PULL_DOWN,
} gpio_port_pull_t;

bool gpio_port_is_valid_pin(gpio_num_t pin);
bool gpio_port_is_valid_output_pin(gpio_num_t pin);
esp_err_t gpio_port_configure(gpio_num_t pin, gpio_port_mode_t mode, gpio_port_pull_t pull);
esp_err_t gpio_port_read(gpio_num_t pin, bool *level);
esp_err_t gpio_port_write(gpio_num_t pin, bool level);
