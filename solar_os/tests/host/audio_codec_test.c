#include <assert.h>
#include <stdint.h>

#include "solar_os_audio_codec.h"
#include "solar_os_audio_pcm.h"

static void test_pcm_conversion(void)
{
    const solar_os_stream_audio_format_t source = {
        .sample_format = SOLAR_OS_STREAM_AUDIO_S16_LE,
        .sample_rate = 16000U,
        .channels = 2U,
        .bits_per_sample = 16U,
    };
    const solar_os_stream_audio_format_t target = {
        .sample_format = SOLAR_OS_STREAM_AUDIO_S16_LE,
        .sample_rate = 16000U,
        .channels = 1U,
        .bits_per_sample = 16U,
    };
    const int16_t input[] = {1000, -1000, 3000, 1000};
    int16_t output[2] = {0};
    size_t output_samples = 0U;
    bool source_done = false;
    solar_os_audio_s16_converter_t converter = {0};
    assert(solar_os_audio_s16_convert(&converter,
                                      input,
                                      2U,
                                      &source,
                                      &target,
                                      output,
                                      2U,
                                      &output_samples,
                                      &source_done) == ESP_OK);
    assert(source_done);
    assert(output_samples == 2U);
    assert(output[0] == 0);
    assert(output[1] == 2000);
}

static void test_pcm_mono_to_stereo_conversion(void)
{
    const solar_os_stream_audio_format_t source = {
        .sample_format = SOLAR_OS_STREAM_AUDIO_S16_LE,
        .sample_rate = 16000U,
        .channels = 1U,
        .bits_per_sample = 16U,
    };
    const solar_os_stream_audio_format_t target = {
        .sample_format = SOLAR_OS_STREAM_AUDIO_S16_LE,
        .sample_rate = 16000U,
        .channels = 2U,
        .bits_per_sample = 16U,
    };
    const int16_t input[] = {-2000, 3000};
    int16_t output[4] = {0};
    size_t output_samples = 0U;
    bool source_done = false;
    solar_os_audio_s16_converter_t converter = {0};
    assert(solar_os_audio_s16_convert(&converter,
                                      input,
                                      2U,
                                      &source,
                                      &target,
                                      output,
                                      4U,
                                      &output_samples,
                                      &source_done) == ESP_OK);
    assert(source_done);
    assert(output_samples == 4U);
    assert(output[0] == -2000);
    assert(output[1] == -2000);
    assert(output[2] == 3000);
    assert(output[3] == 3000);
}

int main(void)
{
    test_pcm_conversion();
    test_pcm_mono_to_stereo_conversion();
    const uint8_t mp3_header[] = {0xffU, 0xfbU, 0x90U, 0x64U};
    solar_os_stream_audio_format_t format;
    assert(solar_os_audio_mp3_probe(mp3_header, sizeof(mp3_header), &format) == ESP_OK);
    assert(format.sample_format == SOLAR_OS_STREAM_AUDIO_S16_LE);
    assert(format.sample_rate == 44100U);
    assert(format.channels == 2U);
    assert(format.bits_per_sample == 16U);

    const uint8_t invalid[] = {0x00U, 0x11U, 0x22U, 0x33U};
    assert(solar_os_audio_mp3_probe(invalid, sizeof(invalid), &format) ==
           ESP_ERR_INVALID_RESPONSE);

    solar_os_audio_mp3_decoder_t *decoder = NULL;
    assert(solar_os_audio_mp3_decoder_create(&decoder) == ESP_OK);
    assert(decoder != NULL);
    int16_t pcm[SOLAR_OS_AUDIO_MP3_MAX_PCM_SAMPLES];
    size_t consumed = 0U;
    solar_os_audio_decoded_frame_t frame;
    assert(solar_os_audio_mp3_decode(decoder,
                                     invalid,
                                     sizeof(invalid),
                                     &consumed,
                                     pcm,
                                     SOLAR_OS_AUDIO_MP3_MAX_PCM_SAMPLES,
                                     &frame) == ESP_OK);
    assert(frame.frames == 0U);
    solar_os_audio_mp3_decoder_destroy(decoder);
    return 0;
}
