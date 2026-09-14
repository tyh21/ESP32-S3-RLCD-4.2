#include "solar_os_shell_line.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool shell_line_paste_byte(const char *text,
                                  size_t index,
                                  uint8_t *normalized)
{
    const uint8_t value = (uint8_t)text[index];

    if (value == '\n' && index > 0 && text[index - 1] == '\r') {
        return false;
    }
    if (value == '\r' || value == '\n' || value == '\t') {
        *normalized = ' ';
        return true;
    }
    if ((value >= 0x20U && value <= 0x7eU) || value >= 0xa0U) {
        *normalized = value;
        return true;
    }
    return false;
}

size_t solar_os_shell_line_paste(char *line,
                                 size_t capacity,
                                 size_t *length,
                                 size_t *cursor,
                                 const char *text,
                                 size_t text_len)
{
    if (line == NULL || capacity == 0 || length == NULL || cursor == NULL ||
        text == NULL || *length >= capacity || *cursor > *length) {
        return 0;
    }

    const size_t available = capacity - *length - 1U;
    size_t insert_len = 0;
    for (size_t i = 0; i < text_len && insert_len < available; i++) {
        uint8_t normalized = 0;
        if (shell_line_paste_byte(text, i, &normalized)) {
            insert_len++;
        }
    }
    if (insert_len == 0) {
        return 0;
    }

    memmove(&line[*cursor + insert_len],
            &line[*cursor],
            *length - *cursor + 1U);

    size_t written = 0;
    for (size_t i = 0; i < text_len && written < insert_len; i++) {
        uint8_t normalized = 0;
        if (shell_line_paste_byte(text, i, &normalized)) {
            line[*cursor + written] = (char)normalized;
            written++;
        }
    }

    *cursor += written;
    *length += written;
    return written;
}
