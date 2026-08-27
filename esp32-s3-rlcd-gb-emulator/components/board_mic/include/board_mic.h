#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t channel_count;
    uint8_t bits_per_sample;
    float mic_gain_db;
} board_mic_config_t;

#define BOARD_MIC_DEFAULT_CONFIG() { \
    .sample_rate_hz = 16000, \
    .channel_count = 2, \
    .bits_per_sample = 16, \
    .mic_gain_db = 35.0f, \
}

esp_err_t board_mic_init(const board_mic_config_t *config);
esp_err_t board_mic_read(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms);
esp_err_t board_mic_set_gain(float gain_db);
esp_err_t board_mic_self_test(void);
void board_mic_deinit(void);

#ifdef __cplusplus
}
#endif
