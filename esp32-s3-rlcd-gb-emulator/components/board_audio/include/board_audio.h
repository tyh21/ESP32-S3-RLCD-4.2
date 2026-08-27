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
    uint8_t speaker_volume;
} board_audio_config_t;

#define BOARD_AUDIO_DEFAULT_CONFIG() { \
    .sample_rate_hz = 16000, \
    .channel_count = 2, \
    .bits_per_sample = 16, \
    .mic_gain_db = 35.0f, \
    .speaker_volume = 70, \
}

esp_err_t board_audio_init(const board_audio_config_t *config);
esp_err_t board_audio_read_mic(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms);
esp_err_t board_audio_write_speaker(const void *buffer, size_t buffer_size, size_t *bytes_written, uint32_t timeout_ms);
esp_err_t board_audio_set_mic_gain(float gain_db);
esp_err_t board_audio_set_speaker_volume(uint8_t volume);
esp_err_t board_audio_speaker_self_test(void);
void board_audio_deinit(void);

#ifdef __cplusplus
}
#endif
