#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_HID_KEY_MODIFIER_FLAG 0x100U
#define SOLAR_OS_HID_KEY_LEFT_CTRL  (SOLAR_OS_HID_KEY_MODIFIER_FLAG | 0x01U)
#define SOLAR_OS_HID_KEY_LEFT_SHIFT (SOLAR_OS_HID_KEY_MODIFIER_FLAG | 0x02U)
#define SOLAR_OS_HID_KEY_LEFT_ALT   (SOLAR_OS_HID_KEY_MODIFIER_FLAG | 0x04U)
#define SOLAR_OS_HID_KEY_LEFT_GUI   (SOLAR_OS_HID_KEY_MODIFIER_FLAG | 0x08U)
#define SOLAR_OS_HID_KEY_RIGHT_CTRL (SOLAR_OS_HID_KEY_MODIFIER_FLAG | 0x10U)
#define SOLAR_OS_HID_KEY_RIGHT_SHIFT (SOLAR_OS_HID_KEY_MODIFIER_FLAG | 0x20U)
#define SOLAR_OS_HID_KEY_RIGHT_ALT  (SOLAR_OS_HID_KEY_MODIFIER_FLAG | 0x40U)
#define SOLAR_OS_HID_KEY_RIGHT_GUI  (SOLAR_OS_HID_KEY_MODIFIER_FLAG | 0x80U)

#define SOLAR_OS_HID_MOUSE_LEFT   0x01U
#define SOLAR_OS_HID_MOUSE_RIGHT  0x02U
#define SOLAR_OS_HID_MOUSE_MIDDLE 0x04U
#define SOLAR_OS_HID_MOUSE_BACK   0x08U
#define SOLAR_OS_HID_MOUSE_FORWARD 0x10U

typedef enum {
    SOLAR_OS_HID_AXIS_X = 0,
    SOLAR_OS_HID_AXIS_Y,
    SOLAR_OS_HID_AXIS_Z,
    SOLAR_OS_HID_AXIS_RZ,
    SOLAR_OS_HID_AXIS_RX,
    SOLAR_OS_HID_AXIS_RY,
    SOLAR_OS_HID_AXIS_COUNT,
} solar_os_hid_axis_t;

typedef enum {
    SOLAR_OS_HID_HAT_CENTERED = 0,
    SOLAR_OS_HID_HAT_UP = 1,
    SOLAR_OS_HID_HAT_UP_RIGHT = 2,
    SOLAR_OS_HID_HAT_RIGHT = 3,
    SOLAR_OS_HID_HAT_DOWN_RIGHT = 4,
    SOLAR_OS_HID_HAT_DOWN = 5,
    SOLAR_OS_HID_HAT_DOWN_LEFT = 6,
    SOLAR_OS_HID_HAT_LEFT = 7,
    SOLAR_OS_HID_HAT_UP_LEFT = 8,
} solar_os_hid_hat_t;

typedef struct {
    bool initialized;
    bool connected;
} solar_os_hid_status_t;

esp_err_t solar_os_hid_init(void);
void solar_os_hid_get_status(solar_os_hid_status_t *status);

esp_err_t solar_os_hid_keyboard_press(const uint16_t *keys, size_t key_count);
esp_err_t solar_os_hid_keyboard_release(const uint16_t *keys, size_t key_count);
esp_err_t solar_os_hid_keyboard_release_all(void);

esp_err_t solar_os_hid_mouse_move(int32_t x, int32_t y);
esp_err_t solar_os_hid_mouse_button(uint8_t button, bool pressed);

esp_err_t solar_os_hid_gamepad_axis(solar_os_hid_axis_t axis, int16_t value);
esp_err_t solar_os_hid_gamepad_button(uint8_t button, bool pressed);
esp_err_t solar_os_hid_gamepad_hat(solar_os_hid_hat_t hat);
esp_err_t solar_os_hid_gamepad_send(void);

/* Neutralize every report type. Intended for script/job lifecycle cleanup. */
void solar_os_hid_release_all(void);
