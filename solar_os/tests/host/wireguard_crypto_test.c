#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crypto.h"

static uint8_t hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return (uint8_t)(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return (uint8_t)(value - 'a' + 10);
    }
    assert(false);
    return 0U;
}

static void decode_hex(const char *text, uint8_t *output, size_t output_len)
{
    assert(strlen(text) == output_len * 2U);
    for (size_t i = 0; i < output_len; i++) {
        output[i] = (uint8_t)((hex_nibble(text[i * 2U]) << 4U) |
                              hex_nibble(text[i * 2U + 1U]));
    }
}

int main(void)
{
    static const char scalar_text[] =
        "77076d0a7318a57d3c16c17251b26645"
        "df4c2f87ebc0992ab177fba51db92c2a";
    static const char public_text[] =
        "8520f0098930a754748b7ddcb43ef75a"
        "0dbf3a0d26381af4eba4a98eaa9b4e6a";
    static const uint8_t basepoint[32] = {9U};
    uint8_t scalar[32];
    uint8_t expected[32];
    uint8_t actual[32];
    static const uint8_t profile_private[32] = {
        0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
        0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU, 0x10U,
        0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U, 0x18U,
        0x19U, 0x1aU, 0x1bU, 0x1cU, 0x1dU, 0x1eU, 0x1fU, 0x20U,
    };
    static const uint8_t profile_peer_public[32] = {
        0x58U, 0x69U, 0xafU, 0xf4U, 0x50U, 0x54U, 0x97U, 0x32U,
        0xcbU, 0xaaU, 0xedU, 0x5eU, 0x5dU, 0xf9U, 0xb3U, 0x0aU,
        0x6dU, 0xa3U, 0x1cU, 0xb0U, 0xe5U, 0x74U, 0x2bU, 0xadU,
        0x5aU, 0xd4U, 0xa1U, 0xa7U, 0x68U, 0xf1U, 0xa6U, 0x7bU,
    };

    decode_hex(scalar_text, scalar, sizeof(scalar));
    decode_hex(public_text, expected, sizeof(expected));
    assert(wireguard_x25519(actual, scalar, basepoint) == 0);
    assert(crypto_equal(actual, expected, sizeof(actual)));
    assert(wireguard_x25519(actual,
                           profile_private,
                           profile_peer_public) == 0);

    crypto_zero(scalar, sizeof(scalar));
    for (size_t i = 0; i < sizeof(scalar); i++) {
        assert(scalar[i] == 0U);
    }

    puts("wireguard crypto tests: ok");
    return 0;
}
