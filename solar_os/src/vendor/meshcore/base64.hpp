#pragma once

#include <stddef.h>
#include <stdint.h>

static inline int meshcore_base64_value(unsigned char value)
{
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '+') {
        return 62;
    }
    if (value == '/') {
        return 63;
    }
    return -1;
}

static inline int decode_base64(const unsigned char *input,
                                size_t input_length,
                                unsigned char *output)
{
    if (input == nullptr || output == nullptr || input_length % 4U != 0U) {
        return -1;
    }
    size_t written = 0;
    for (size_t offset = 0; offset < input_length; offset += 4U) {
        int values[4] = {0, 0, 0, 0};
        size_t padding = 0;
        for (size_t index = 0; index < 4U; index++) {
            if (input[offset + index] == '=') {
                padding++;
                continue;
            }
            values[index] = meshcore_base64_value(input[offset + index]);
            if (values[index] < 0 || padding != 0U) {
                return -1;
            }
        }
        if (padding > 2U || (padding != 0U && offset + 4U != input_length)) {
            return -1;
        }
        output[written++] =
            (unsigned char)((values[0] << 2) | (values[1] >> 4));
        if (padding < 2U) {
            output[written++] =
                (unsigned char)((values[1] << 4) | (values[2] >> 2));
        }
        if (padding == 0U) {
            output[written++] =
                (unsigned char)((values[2] << 6) | values[3]);
        }
    }
    return (int)written;
}
