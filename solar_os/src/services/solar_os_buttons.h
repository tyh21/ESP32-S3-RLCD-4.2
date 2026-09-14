#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef enum {
    SOLAR_OS_BUTTON_PULL_NONE = 0,
    SOLAR_OS_BUTTON_PULL_UP,
    SOLAR_OS_BUTTON_PULL_DOWN,
} solar_os_button_pull_t;

typedef struct {
    gpio_num_t pin;
    const char *name;
    uint8_t key;
    bool active_low;
    bool emit_on_release;
    solar_os_button_pull_t pull;
} solar_os_button_def_t;

esp_err_t solar_os_buttons_init(void);
void solar_os_buttons_poll(void);
size_t solar_os_buttons_read_chars(char *buffer, size_t buffer_len);
