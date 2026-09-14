#pragma once

#include <stdint.h>

typedef enum {
    SOLAR_OS_PIN_POLICY_FIXED = 0,
    SOLAR_OS_PIN_POLICY_RELEASABLE,
    SOLAR_OS_PIN_POLICY_FREE,
} solar_os_pin_policy_t;

typedef struct {
    int pin;
    solar_os_pin_policy_t policy;
    const char *role;
} solar_os_board_pin_t;

typedef enum {
    SOLAR_OS_CONNECTOR_PIN_GPIO = 0,
    SOLAR_OS_CONNECTOR_PIN_POWER,
    SOLAR_OS_CONNECTOR_PIN_GROUND,
    SOLAR_OS_CONNECTOR_PIN_CONTROL,
    SOLAR_OS_CONNECTOR_PIN_NC,
} solar_os_connector_pin_kind_t;

typedef struct {
    const char *connector;
    uint8_t position;
    uint8_t row;
    uint8_t column;
    int pin;
    solar_os_connector_pin_kind_t kind;
    const char *label;
} solar_os_board_connector_pin_t;
