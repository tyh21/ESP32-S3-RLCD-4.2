#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "solar_os_dsp.h"

static void test_stateless(void)
{
    const int16_t a[] = {INT16_MIN, -1000, 1000, INT16_MAX};
    const int16_t b[] = {INT16_MIN, 2000, -2000, INT16_MAX};
    int64_t dot = 0;
    assert(solar_os_dsp_dot_s16(a, b, 4, &dot) == ESP_OK);
    assert(dot == (int64_t)INT16_MIN * INT16_MIN - 4000000LL +
                  (int64_t)INT16_MAX * INT16_MAX);

    int16_t output[4];
    assert(solar_os_dsp_gain_q15(output, a, 4, 16384) == ESP_OK);
    assert(output[0] == -16384 && output[1] == -500 &&
           output[2] == 500 && output[3] == 16383);
    assert(solar_os_dsp_gain_q15(output, a, 4, INT16_MIN) == ESP_OK);
    assert(output[0] == INT16_MAX);

    assert(solar_os_dsp_mix_q15(output, a, b, 4, 32767, 32767) == ESP_OK);
    assert(output[0] == INT16_MIN && output[3] == INT16_MAX);

    assert(solar_os_dsp_clip_s16(output, a, 4, -900, 900) == ESP_OK);
    assert(output[0] == -900 && output[1] == -900 &&
           output[2] == 900 && output[3] == 900);

    solar_os_dsp_level_t level;
    const int16_t levels[] = {-3, 4};
    assert(solar_os_dsp_level_s16(levels, 2, &level) == ESP_OK);
    assert(level.peak == 4 && level.rms == 3);

    const int16_t window[] = {32767, 16384, 0, INT16_MIN};
    assert(solar_os_dsp_window_q15(output, a, window, 4) == ESP_OK);
    assert(output[0] == -32767 && output[1] == -500 &&
           output[2] == 0 && output[3] == -32767);

    int16_t partial[6] = {0};
    assert(solar_os_dsp_gain_q15(partial + 1, partial, 4, 1) == ESP_ERR_INVALID_ARG);
    assert(solar_os_dsp_dot_s16(NULL, NULL, 0, &dot) == ESP_OK && dot == 0);
    assert(solar_os_dsp_level_s16(NULL, 0, &level) == ESP_ERR_INVALID_ARG);

    int16_t in_place[] = {INT16_MIN, -1, 1, INT16_MAX};
    assert(solar_os_dsp_gain_q15(in_place, in_place, 4, 16384) == ESP_OK);
    assert(in_place[0] == -16384 && in_place[1] == -1 &&
           in_place[2] == 0 && in_place[3] == 16383);
}

static void test_fir(void)
{
    const int16_t coefficients[] = {16384, 16384};
    const int16_t input[] = {1000, 2000, 3000, 4000};
    int16_t output[4] = {0};
    solar_os_dsp_fir_t *fir = NULL;
    assert(solar_os_dsp_fir_create(coefficients, 2, &fir) == ESP_OK);
    assert(solar_os_dsp_fir_taps(fir) == 2);
    assert(solar_os_dsp_fir_process(fir, output, input, 4) == ESP_OK);
    const int16_t expected[] = {500, 1500, 2500, 3500};
    assert(memcmp(output, expected, sizeof(expected)) == 0);
    solar_os_dsp_fir_reset(fir);
    assert(solar_os_dsp_fir_process(fir, output, input, 1) == ESP_OK);
    assert(output[0] == 500);
    solar_os_dsp_fir_destroy(fir);

    solar_os_dsp_fir_t *chunked = NULL;
    assert(solar_os_dsp_fir_create(coefficients, 2, &chunked) == ESP_OK);
    int16_t chunked_output[4] = {0};
    assert(solar_os_dsp_fir_process(chunked, chunked_output, input, 2) == ESP_OK);
    assert(solar_os_dsp_fir_process(chunked, chunked_output + 2,
                                    input + 2, 2) == ESP_OK);
    assert(memcmp(chunked_output, expected, sizeof(expected)) == 0);
    solar_os_dsp_fir_destroy(chunked);

    assert(solar_os_dsp_fir_create(coefficients,
                                    SOLAR_OS_DSP_MAX_TAPS + 1U,
                                    &fir) == ESP_ERR_INVALID_ARG);
}

static void test_decimator(void)
{
    const int16_t coefficient[] = {32767};
    const int16_t first[] = {10, 20, 30};
    const int16_t second[] = {40, 50, 60};
    int16_t output[3] = {0};
    size_t produced = 0;
    solar_os_dsp_decimator_t *decimator = NULL;
    assert(solar_os_dsp_decimator_create(coefficient, 1, 2, &decimator) == ESP_OK);
    assert(solar_os_dsp_decimator_output_count(decimator, 3) == 2);
    produced = 99;
    assert(solar_os_dsp_decimator_process(decimator, output, 1, first, 3,
                                           &produced) == ESP_ERR_INVALID_SIZE);
    assert(produced == 99);
    assert(solar_os_dsp_decimator_output_count(decimator, 3) == 2);
    assert(solar_os_dsp_decimator_process(decimator, output, 3, first, 3,
                                           &produced) == ESP_OK);
    assert(produced == 2 && output[0] == 9 && output[1] == 29);
    assert(solar_os_dsp_decimator_output_count(decimator, 3) == 1);
    assert(solar_os_dsp_decimator_process(decimator, output, 3, second, 3,
                                           &produced) == ESP_OK);
    assert(produced == 1 && output[0] == 49);
    solar_os_dsp_decimator_destroy(decimator);
}

static void test_fft(void)
{
    solar_os_dsp_fft_t *fft = NULL;
    assert(solar_os_dsp_fft_create(8, &fft) == ESP_OK);
    const int16_t impulse[] = {32767, 0, 0, 0, 0, 0, 0, 0};
    solar_os_dsp_complex_s16_t spectrum[8];
    uint8_t exponent = 0;
    assert(solar_os_dsp_fft_execute(fft, spectrum, impulse, &exponent) == ESP_OK);
    assert(exponent == 3);
    for (size_t i = 0; i < 8; i++) {
        assert(spectrum[i].real == 4095);
        assert(spectrum[i].imag == 0);
    }
    assert(solar_os_dsp_fft_size(fft) == 8);
    solar_os_dsp_fft_destroy(fft);
    assert(solar_os_dsp_fft_create(7, &fft) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    assert(strcmp(solar_os_dsp_backend(), "portable") == 0);
    assert(solar_os_dsp_capabilities() != 0U);
    assert(solar_os_dsp_accelerated_capabilities() == 0U);
    test_stateless();
    test_fir();
    test_decimator();
    test_fft();
    puts("dsp_test: ok");
    return 0;
}
