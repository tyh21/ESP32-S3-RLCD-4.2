#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_stream.h"

#define SOLAR_OS_SYNTH_BLOCK_FRAMES_DEFAULT 256U
#define SOLAR_OS_SYNTH_BLOCK_FRAMES_MIN 32U
#define SOLAR_OS_SYNTH_BLOCK_FRAMES_MAX 512U
#define SOLAR_OS_SYNTH_OWNER_MAX 24U

/* Render exactly frames of signed 16-bit interleaved stereo PCM. */
typedef void (*solar_os_synth_render_cb_t)(int16_t *samples,
                                           size_t frames,
                                           uint32_t sample_rate,
                                           void *user);

typedef struct {
    const char *owner;
    /* NULL or empty selects the current default audio output. */
    const char *playback_stream;
    solar_os_synth_render_cb_t render;
    void *user;
    size_t block_frames;
} solar_os_synth_config_t;

typedef struct {
    bool starting;
    bool running;
    char owner[SOLAR_OS_SYNTH_OWNER_MAX];
    char playback_stream[SOLAR_OS_STREAM_ID_MAX];
    uint32_t sample_rate;
    size_t block_frames;
    uint64_t rendered_frames;
    uint32_t rendered_blocks;
    uint32_t render_deadline_misses;
    uint32_t write_errors;
    uint32_t max_render_us;
    esp_err_t last_error;
} solar_os_synth_status_t;

/* One client owns the synthesizer and audio output at a time. */
esp_err_t solar_os_synth_start(const solar_os_synth_config_t *config);
esp_err_t solar_os_synth_stop(const char *owner);
void solar_os_synth_get_status(solar_os_synth_status_t *status);
