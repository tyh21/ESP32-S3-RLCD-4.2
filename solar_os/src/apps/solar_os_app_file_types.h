#pragma once

#include <stdbool.h>

/* Extensions are a space-separated list including the leading dot. */
bool solar_os_app_file_types_match(const char *extensions, const char *path);
