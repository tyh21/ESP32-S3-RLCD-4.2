#include "gpio_port.h"

#include <stdint.h>

bool gpio_port_is_valid_pin(gpio_num_t pin)
{
    return GPIO_IS_VALID_GPIO(pin);
}

bool gpio_port_is_valid_output_pin(gpio_num_t pin)
{
    return GPIO_IS_VALID_OUTPUT_GPIO(pin);
}

esp_err_t gpio_port_configure(gpio_num_t pin, gpio_port_mode_t mode, gpio_port_pull_t pull)
{
    if (!gpio_port_is_valid_pin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mode == GPIO_PORT_MODE_OUTPUT && !gpio_port_is_valid_output_pin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << (uint32_t)pin,
        .mode = mode == GPIO_PORT_MODE_OUTPUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
        .pull_up_en = pull == GPIO_PORT_PULL_UP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull == GPIO_PORT_PULL_DOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

esp_err_t gpio_port_read(gpio_num_t pin, bool *level)
{
    if (level == NULL || !gpio_port_is_valid_pin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    *level = gpio_get_level(pin) != 0;
    return ESP_OK;
}

esp_err_t gpio_port_write(gpio_num_t pin, bool level)
{
    if (!gpio_port_is_valid_output_pin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    return gpio_set_level(pin, level ? 1 : 0);
}
