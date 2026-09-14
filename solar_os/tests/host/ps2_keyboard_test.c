#include <assert.h>
#include <stdio.h>

#include "solar_os_ps2_keyboard.h"

static solar_os_ps2_key_transition_t feed_key(solar_os_ps2_keyboard_decoder_t *decoder,
                                               uint8_t byte)
{
    solar_os_ps2_key_transition_t transition = {0};
    assert(solar_os_ps2_keyboard_decode(decoder, byte, &transition) ==
           SOLAR_OS_PS2_DECODE_KEY);
    return transition;
}

int main(void)
{
    solar_os_ps2_keyboard_decoder_t decoder;
    solar_os_ps2_keyboard_decoder_reset(&decoder);

    solar_os_ps2_key_transition_t transition = feed_key(&decoder, 0x1c);
    assert(transition.usage == 0x04);
    assert(transition.pressed);

    assert(solar_os_ps2_keyboard_decode(&decoder, 0xf0, &transition) ==
           SOLAR_OS_PS2_DECODE_NONE);
    transition = feed_key(&decoder, 0x1c);
    assert(transition.usage == 0x04);
    assert(!transition.pressed);

    assert(solar_os_ps2_keyboard_decode(&decoder, 0xe0, &transition) ==
           SOLAR_OS_PS2_DECODE_NONE);
    transition = feed_key(&decoder, 0x75);
    assert(transition.usage == 0x52);
    assert(transition.pressed);

    assert(solar_os_ps2_keyboard_decode(&decoder, 0xe0, &transition) ==
           SOLAR_OS_PS2_DECODE_NONE);
    assert(solar_os_ps2_keyboard_decode(&decoder, 0xf0, &transition) ==
           SOLAR_OS_PS2_DECODE_NONE);
    transition = feed_key(&decoder, 0x75);
    assert(transition.usage == 0x52);
    assert(!transition.pressed);

    assert(solar_os_ps2_keyboard_decode(&decoder, 0xe0, &transition) ==
           SOLAR_OS_PS2_DECODE_NONE);
    assert(solar_os_ps2_keyboard_decode(&decoder, 0x12, &transition) ==
           SOLAR_OS_PS2_DECODE_NONE);
    assert(solar_os_ps2_keyboard_decode(&decoder, 0xe0, &transition) ==
           SOLAR_OS_PS2_DECODE_NONE);
    transition = feed_key(&decoder, 0x7c);
    assert(transition.usage == 0x46);

    puts("ps2 keyboard decoder tests: ok");
    return 0;
}
