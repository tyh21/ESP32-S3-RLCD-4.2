#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SOLAR_OS_PS2_DECODE_NONE,
    SOLAR_OS_PS2_DECODE_KEY,
    SOLAR_OS_PS2_DECODE_UNSUPPORTED,
} solar_os_ps2_decode_result_t;

typedef struct {
    bool extended;
    bool release;
    uint8_t e1_remaining;
} solar_os_ps2_keyboard_decoder_t;

typedef struct {
    uint16_t usage;
    bool pressed;
} solar_os_ps2_key_transition_t;

void solar_os_ps2_keyboard_decoder_reset(solar_os_ps2_keyboard_decoder_t *decoder);
solar_os_ps2_decode_result_t solar_os_ps2_keyboard_decode(
    solar_os_ps2_keyboard_decoder_t *decoder,
    uint8_t byte,
    solar_os_ps2_key_transition_t *transition);
