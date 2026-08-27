#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    float humidity_percent;
} board_shtc3_data_t;

esp_err_t board_shtc3_init(void);
esp_err_t board_shtc3_read(board_shtc3_data_t *data);
esp_err_t board_shtc3_sleep(void);
void board_shtc3_release(void);
void board_shtc3_deinit(void);

#ifdef __cplusplus
}
#endif
