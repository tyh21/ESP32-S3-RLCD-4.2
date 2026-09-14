#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_stream.h"

#define SOLAR_OS_AUDIO_MP3_MAX_PCM_SAMPLES 2304U
/*
 * Retain at least this much live-stream data before asking minimp3 to find the
 * next frame. A shorter incomplete tail can be reported as consumed sync
 * garbage, which loses the frame boundary when the next network chunk arrives.
 */
#define SOLAR_OS_AUDIO_MP3_STREAM_WINDOW_BYTES 4096U

typedef struct solar_os_audio_mp3_decoder solar_os_audio_mp3_decoder_t;

typedef struct {
    solar_os_stream_audio_format_t format;
    size_t frames;
    size_t samples;
} solar_os_audio_decoded_frame_t;

/*
 * The decoder has no file, network, or playback-device ownership. The caller
 * retains bytes that were not consumed and routes decoded PCM to its chosen
 * destination.
 */
esp_err_t solar_os_audio_mp3_decoder_create(
    solar_os_audio_mp3_decoder_t **decoder);
void solar_os_audio_mp3_decoder_destroy(
    solar_os_audio_mp3_decoder_t *decoder);
esp_err_t solar_os_audio_mp3_decode(
    solar_os_audio_mp3_decoder_t *decoder,
    const uint8_t *input,
    size_t input_len,
    size_t *consumed,
    int16_t *pcm,
    size_t pcm_capacity_samples,
    solar_os_audio_decoded_frame_t *frame);

/* Probe a byte window for the first supported MP3 frame header. */
esp_err_t solar_os_audio_mp3_probe(const uint8_t *input,
                                   size_t input_len,
                                   solar_os_stream_audio_format_t *format);
