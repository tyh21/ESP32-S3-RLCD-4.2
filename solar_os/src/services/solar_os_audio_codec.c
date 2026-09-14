#include "solar_os_audio_codec.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "minimp3.h"
#if !defined(SOLAR_OS_AUDIO_CODEC_HOST_TEST)
#include "solar_os_memory.h"
#endif

#define AUDIO_MP3_MIN_SAMPLE_RATE 8000U

struct solar_os_audio_mp3_decoder {
    mp3dec_t decoder;
};

static void *audio_codec_alloc(size_t size)
{
#if defined(SOLAR_OS_AUDIO_CODEC_HOST_TEST)
    return malloc(size);
#else
    return solar_os_memory_alloc(size,
                                 SOLAR_OS_MEMORY_EXTERNAL_PREFERRED,
                                 "audio.mp3.decoder");
#endif
}

static void audio_codec_free(void *memory)
{
#if defined(SOLAR_OS_AUDIO_CODEC_HOST_TEST)
    free(memory);
#else
    solar_os_memory_free(memory);
#endif
}

static bool audio_mp3_parse_frame_header(
    uint32_t header,
    solar_os_stream_audio_format_t *format)
{
    if ((header & 0xffe00000U) != 0xffe00000U) {
        return false;
    }

    const uint8_t version = (uint8_t)((header >> 19) & 0x03U);
    const uint8_t layer = (uint8_t)((header >> 17) & 0x03U);
    const uint8_t bitrate_index = (uint8_t)((header >> 12) & 0x0fU);
    const uint8_t sample_rate_index = (uint8_t)((header >> 10) & 0x03U);
    if (version == 1U || layer != 1U || bitrate_index == 0U ||
        bitrate_index == 15U || sample_rate_index == 3U) {
        return false;
    }

    static const uint32_t base_rates[] = {44100U, 48000U, 32000U};
    uint32_t sample_rate = base_rates[sample_rate_index];
    if (version == 2U) {
        sample_rate /= 2U;
    } else if (version == 0U) {
        sample_rate /= 4U;
    }
    if (sample_rate < AUDIO_MP3_MIN_SAMPLE_RATE) {
        return false;
    }

    *format = (solar_os_stream_audio_format_t){
        .sample_format = SOLAR_OS_STREAM_AUDIO_S16_LE,
        .sample_rate = sample_rate,
        .channels = ((header >> 6) & 0x03U) == 3U ? 1U : 2U,
        .bits_per_sample = 16U,
    };
    return true;
}

esp_err_t solar_os_audio_mp3_decoder_create(
    solar_os_audio_mp3_decoder_t **decoder)
{
    if (decoder == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *decoder = audio_codec_alloc(sizeof(**decoder));
    if (*decoder == NULL) {
        return ESP_ERR_NO_MEM;
    }
    mp3dec_init(&(*decoder)->decoder);
    return ESP_OK;
}

void solar_os_audio_mp3_decoder_destroy(
    solar_os_audio_mp3_decoder_t *decoder)
{
    audio_codec_free(decoder);
}

esp_err_t solar_os_audio_mp3_decode(
    solar_os_audio_mp3_decoder_t *decoder,
    const uint8_t *input,
    size_t input_len,
    size_t *consumed,
    int16_t *pcm,
    size_t pcm_capacity_samples,
    solar_os_audio_decoded_frame_t *frame)
{
    if (decoder == NULL || input == NULL || consumed == NULL || pcm == NULL ||
        frame == NULL || pcm_capacity_samples < SOLAR_OS_AUDIO_MP3_MAX_PCM_SAMPLES ||
        input_len > INT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    *consumed = 0U;
    memset(frame, 0, sizeof(*frame));
    mp3dec_frame_info_t decoded = {0};
    const int frames = mp3dec_decode_frame(&decoder->decoder,
                                           input,
                                           (int)input_len,
                                           pcm,
                                           &decoded);
    if (decoded.frame_bytes > 0) {
        *consumed = (size_t)decoded.frame_bytes;
        if (*consumed > input_len) {
            *consumed = input_len;
        }
    }
    if (frames <= 0) {
        return ESP_OK;
    }
    if (decoded.hz < (int)AUDIO_MP3_MIN_SAMPLE_RATE || decoded.channels <= 0 ||
        decoded.channels > 2) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    frame->format = (solar_os_stream_audio_format_t){
        .sample_format = SOLAR_OS_STREAM_AUDIO_S16_LE,
        .sample_rate = (uint32_t)decoded.hz,
        .channels = (uint8_t)decoded.channels,
        .bits_per_sample = 16U,
        .frames_per_block = (uint16_t)frames,
    };
    frame->frames = (size_t)frames;
    frame->samples = frame->frames * frame->format.channels;
    return ESP_OK;
}

esp_err_t solar_os_audio_mp3_probe(const uint8_t *input,
                                   size_t input_len,
                                   solar_os_stream_audio_format_t *format)
{
    if (input == NULL || input_len == 0U || format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t window = 0U;
    size_t have = 0U;
    for (size_t i = 0; i < input_len; i++) {
        window = (window << 8) | input[i];
        if (have < 4U) {
            have++;
        }
        if (have >= 4U && audio_mp3_parse_frame_header(window, format)) {
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_RESPONSE;
}
