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
    uint8_t volume;
} board_speaker_config_t;

#define BOARD_SPEAKER_DEFAULT_CONFIG() { \
    .sample_rate_hz = 16000, \
    .channel_count = 2, \
    .bits_per_sample = 16, \
    .volume = 70, \
}

esp_err_t board_speaker_init(const board_speaker_config_t *config);
esp_err_t board_speaker_set_volume(uint8_t volume);
uint8_t board_speaker_get_volume(void);
esp_err_t board_speaker_write(const void *buffer, size_t buffer_size, size_t *bytes_written, uint32_t timeout_ms);
esp_err_t board_speaker_self_test(void);
void board_speaker_deinit(void);

#ifdef __cplusplus
}
#endif
