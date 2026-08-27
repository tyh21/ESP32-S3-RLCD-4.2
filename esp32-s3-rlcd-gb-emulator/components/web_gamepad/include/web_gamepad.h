#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_gamepad_start(void);
uint8_t web_gamepad_get_joypad_state(void);
uint8_t web_gamepad_get_volume(void);

#ifdef __cplusplus
}
#endif
