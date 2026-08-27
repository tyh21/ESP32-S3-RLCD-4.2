#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    time_t unix_seconds;
    int16_t timezone_offset_minutes;
    bool synced;
} board_clock_status_t;

esp_err_t board_clock_sync_unix_ms(uint64_t unix_ms, int16_t timezone_offset_minutes);
board_clock_status_t board_clock_get_status(void);
time_t board_clock_get_phone_local_seconds(void);

#ifdef __cplusplus
}
#endif
