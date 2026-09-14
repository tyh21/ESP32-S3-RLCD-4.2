#include "solar_os_ps2_keyboard.h"

#include <string.h>

static uint16_t ps2_set2_usage(uint8_t code, bool extended)
{
    if (extended) {
        switch (code) {
        case 0x11: return 0xe6; /* right alt */
        case 0x14: return 0xe4; /* right ctrl */
        case 0x1f: return 0xe3; /* left GUI */
        case 0x27: return 0xe7; /* right GUI */
        case 0x2f: return 0x65; /* application */
        case 0x4a: return 0x54; /* keypad / */
        case 0x5a: return 0x58; /* keypad enter */
        case 0x69: return 0x4d; /* end */
        case 0x6b: return 0x50; /* left */
        case 0x6c: return 0x4a; /* home */
        case 0x70: return 0x49; /* insert */
        case 0x71: return 0x4c; /* delete */
        case 0x72: return 0x51; /* down */
        case 0x74: return 0x4f; /* right */
        case 0x75: return 0x52; /* up */
        case 0x7a: return 0x4e; /* page down */
        case 0x7c: return 0x46; /* print screen */
        case 0x7d: return 0x4b; /* page up */
        default: return 0;
        }
    }

    switch (code) {
    case 0x01: return 0x42; case 0x03: return 0x3e;
    case 0x04: return 0x3c; case 0x05: return 0x3a;
    case 0x06: return 0x3b; case 0x07: return 0x45;
    case 0x09: return 0x43; case 0x0a: return 0x41;
    case 0x0b: return 0x3f; case 0x0c: return 0x3d;
    case 0x0d: return 0x2b; case 0x0e: return 0x35;
    case 0x11: return 0xe2; case 0x12: return 0xe1;
    case 0x14: return 0xe0; case 0x15: return 0x14;
    case 0x16: return 0x1e; case 0x1a: return 0x1d;
    case 0x1b: return 0x16; case 0x1c: return 0x04;
    case 0x1d: return 0x1a; case 0x1e: return 0x1f;
    case 0x21: return 0x06; case 0x22: return 0x1b;
    case 0x23: return 0x07; case 0x24: return 0x08;
    case 0x25: return 0x21; case 0x26: return 0x20;
    case 0x29: return 0x2c; case 0x2a: return 0x19;
    case 0x2b: return 0x09; case 0x2c: return 0x17;
    case 0x2d: return 0x15; case 0x2e: return 0x22;
    case 0x31: return 0x11; case 0x32: return 0x05;
    case 0x33: return 0x0b; case 0x34: return 0x0a;
    case 0x35: return 0x1c; case 0x36: return 0x23;
    case 0x3a: return 0x10; case 0x3b: return 0x0d;
    case 0x3c: return 0x18; case 0x3d: return 0x24;
    case 0x3e: return 0x25; case 0x41: return 0x36;
    case 0x42: return 0x0e; case 0x43: return 0x0c;
    case 0x44: return 0x12; case 0x45: return 0x27;
    case 0x46: return 0x26; case 0x49: return 0x37;
    case 0x4a: return 0x38; case 0x4b: return 0x0f;
    case 0x4c: return 0x33; case 0x4d: return 0x13;
    case 0x4e: return 0x2d; case 0x52: return 0x34;
    case 0x54: return 0x2f; case 0x55: return 0x2e;
    case 0x58: return 0x39; case 0x59: return 0xe5;
    case 0x5a: return 0x28; case 0x5b: return 0x30;
    case 0x5d: return 0x31; case 0x61: return 0x64;
    case 0x66: return 0x2a; case 0x69: return 0x59;
    case 0x6b: return 0x5c; case 0x6c: return 0x5f;
    case 0x70: return 0x62; case 0x71: return 0x63;
    case 0x72: return 0x5a; case 0x73: return 0x5d;
    case 0x74: return 0x5e; case 0x75: return 0x60;
    case 0x76: return 0x29; case 0x77: return 0x53;
    case 0x78: return 0x44; case 0x79: return 0x57;
    case 0x7a: return 0x5b; case 0x7b: return 0x56;
    case 0x7c: return 0x55; case 0x7d: return 0x61;
    case 0x7e: return 0x47; case 0x83: return 0x40;
    default: return 0;
    }
}

void solar_os_ps2_keyboard_decoder_reset(solar_os_ps2_keyboard_decoder_t *decoder)
{
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(*decoder));
    }
}

solar_os_ps2_decode_result_t solar_os_ps2_keyboard_decode(
    solar_os_ps2_keyboard_decoder_t *decoder,
    uint8_t byte,
    solar_os_ps2_key_transition_t *transition)
{
    if (decoder == NULL || transition == NULL) {
        return SOLAR_OS_PS2_DECODE_UNSUPPORTED;
    }
    if (decoder->e1_remaining > 0) {
        decoder->e1_remaining--;
        return SOLAR_OS_PS2_DECODE_NONE;
    }
    if (byte == 0xe1U) {
        /* Pause is an eight-byte make-only sequence; ignore it atomically. */
        decoder->extended = false;
        decoder->release = false;
        decoder->e1_remaining = 7;
        return SOLAR_OS_PS2_DECODE_NONE;
    }
    if (byte == 0xe0U) {
        decoder->extended = true;
        return SOLAR_OS_PS2_DECODE_NONE;
    }
    if (byte == 0xf0U) {
        decoder->release = true;
        return SOLAR_OS_PS2_DECODE_NONE;
    }

    const bool extended = decoder->extended;
    const bool release = decoder->release;
    decoder->extended = false;
    decoder->release = false;
    if (extended && byte == 0x12U) {
        /* Synthetic shift in the Print Screen sequence. */
        return SOLAR_OS_PS2_DECODE_NONE;
    }
    const uint16_t usage = ps2_set2_usage(byte, extended);
    if (usage == 0) {
        return SOLAR_OS_PS2_DECODE_UNSUPPORTED;
    }
    *transition = (solar_os_ps2_key_transition_t) {
        .usage = usage,
        .pressed = !release,
    };
    return SOLAR_OS_PS2_DECODE_KEY;
}
