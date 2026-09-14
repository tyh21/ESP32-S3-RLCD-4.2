#pragma once

#include <stddef.h>

size_t solar_os_shell_line_paste(char *line,
                                 size_t capacity,
                                 size_t *length,
                                 size_t *cursor,
                                 const char *text,
                                 size_t text_len);
