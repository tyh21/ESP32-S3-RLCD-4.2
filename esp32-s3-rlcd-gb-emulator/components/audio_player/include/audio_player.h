#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_PLAYER_FORMAT_UNKNOWN = 0,
    AUDIO_PLAYER_FORMAT_WAV,
    AUDIO_PLAYER_FORMAT_MP3,
    AUDIO_PLAYER_FORMAT_FLAC,
} audio_player_format_t;

typedef enum {
    AUDIO_PLAYER_STATE_IDLE = 0,
    AUDIO_PLAYER_STATE_STARTING,
    AUDIO_PLAYER_STATE_PLAYING,
    AUDIO_PLAYER_STATE_PAUSED,
    AUDIO_PLAYER_STATE_STOPPED,
    AUDIO_PLAYER_STATE_ERROR,
} audio_player_state_t;

/**
 * Start playing an audio file from the SD card.
 * The file format (WAV/MP3) is auto-detected by extension.
 * Playback runs in a dedicated FreeRTOS task; this function returns immediately.
 *
 * @param path  Full path to the audio file (e.g. "/sdcard/music.mp3").
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t audio_player_play(const char *path);

/**
 * Stop playback and free resources.
 */
esp_err_t audio_player_stop(void);

/**
 * Pause playback (can be resumed with audio_player_resume).
 */
esp_err_t audio_player_pause(void);

/**
 * Resume paused playback.
 */
esp_err_t audio_player_resume(void);

/**
 * Set volume (0-100).
 */
esp_err_t audio_player_set_volume(uint8_t volume);

/**
 * Get current volume.
 */
uint8_t audio_player_get_volume(void);

/**
 * Get current playback state.
 */
audio_player_state_t audio_player_get_state(void);

/**
 * Get current playing file path.
 */
const char *audio_player_get_path(void);

/**
 * Get detected format of current file.
 */
audio_player_format_t audio_player_get_format(void);

/**
 * Get sample rate of current file.
 */
uint32_t audio_player_get_sample_rate(void);

/**
 * Check if a file extension is supported.
 */
bool audio_player_is_supported_file(const char *path);

#ifdef __cplusplus
}
#endif
