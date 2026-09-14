#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t solar_os_agent_reference_search(const char *query,
                                          char *result,
                                          size_t result_len);
