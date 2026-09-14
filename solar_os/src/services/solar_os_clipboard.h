#pragma once

#include <stddef.h>

#include "esp_err.h"

#define SOLAR_OS_CLIPBOARD_MAX_BYTES (256 * 1024)

esp_err_t solar_os_clipboard_set(const char *data, size_t len);
const char *solar_os_clipboard_data(size_t *len);
size_t solar_os_clipboard_size(void);
void solar_os_clipboard_clear(void);
