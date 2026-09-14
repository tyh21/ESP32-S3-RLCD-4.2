#include "solar_os_dsp.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef SOLAR_OS_DSP_HOST_TEST
#include "solar_os_board_caps.h"
#include "solar_os_config.h"
#include "solar_os_engines.h"
#include "solar_os_memory.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3) && SOLAR_OS_BOARD_HAS_SIMD
#include "dsps_fft2r.h"
#include "dsps_mul.h"
#define SOLAR_OS_DSP_HAS_ESP32S3_PIE 1
#endif
#endif

#ifndef SOLAR_OS_DSP_HAS_ESP32S3_PIE
#define SOLAR_OS_DSP_HAS_ESP32S3_PIE 0
#endif

#ifndef SOLAR_OS_PACKAGE_SERVICE_ENGINES
#define SOLAR_OS_PACKAGE_SERVICE_ENGINES 0
#endif

#define DSP_ALL_CAPABILITIES \
    (SOLAR_OS_DSP_CAP_DOT_S16 | SOLAR_OS_DSP_CAP_GAIN_Q15 | \
     SOLAR_OS_DSP_CAP_MIX_Q15 | SOLAR_OS_DSP_CAP_CLIP_S16 | \
     SOLAR_OS_DSP_CAP_LEVEL_S16 | SOLAR_OS_DSP_CAP_WINDOW_Q15 | \
     SOLAR_OS_DSP_CAP_FIR_Q15 | SOLAR_OS_DSP_CAP_DECIMATOR_Q15 | \
     SOLAR_OS_DSP_CAP_FFT_S16)

#define DSP_PIE_MIN_SAMPLES 64U
#define DSP_PIE_LANES 8U
#define DSP_PI 3.14159265358979323846

struct solar_os_dsp_fir {
    size_t taps;
    size_t position;
    int16_t *coefficients;
    int16_t *history;
};

struct solar_os_dsp_decimator {
    solar_os_dsp_fir_t *fir;
    size_t factor;
    size_t phase;
};

struct solar_os_dsp_fft {
    size_t size;
    uint8_t stages;
    solar_os_dsp_complex_s16_t *twiddles;
    void *work_allocation;
    solar_os_dsp_complex_s16_t *work;
};

#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
static portMUX_TYPE dsp_fft_init_lock = portMUX_INITIALIZER_UNLOCKED;
static bool dsp_fft_initializing;
static bool dsp_fft_init_attempted;
static bool dsp_fft_accelerator_ready;
static int16_t *dsp_fft_owned_table;
#endif

typedef struct {
#if !defined(SOLAR_OS_DSP_HOST_TEST) && SOLAR_OS_PACKAGE_SERVICE_ENGINES
    solar_os_engine_token_t token;
#endif
    bool active;
} dsp_engine_token_t;

static void *dsp_calloc(size_t count, size_t size, const char *tag)
{
#ifdef SOLAR_OS_DSP_HOST_TEST
    (void)tag;
    return calloc(count, size);
#else
    return solar_os_memory_calloc(count, size,
                                  SOLAR_OS_MEMORY_EXTERNAL_PREFERRED, tag);
#endif
}

static void dsp_free(void *ptr)
{
#ifdef SOLAR_OS_DSP_HOST_TEST
    free(ptr);
#else
    solar_os_memory_free(ptr);
#endif
}

static dsp_engine_token_t dsp_engine_begin(bool accelerated, const char *label)
{
    dsp_engine_token_t token = {0};
#if !defined(SOLAR_OS_DSP_HOST_TEST) && SOLAR_OS_PACKAGE_SERVICE_ENGINES
    const char *engine = accelerated ? "simd" : "cpu";
    token.active = solar_os_engine_begin(engine, "dsp", label, &token.token) == ESP_OK;
#else
    (void)accelerated;
    (void)label;
#endif
    return token;
}

static void dsp_engine_end(dsp_engine_token_t *token, size_t units)
{
#if !defined(SOLAR_OS_DSP_HOST_TEST) && SOLAR_OS_PACKAGE_SERVICE_ENGINES
    if (token != NULL && token->active) {
        (void)solar_os_engine_end(&token->token, units);
    }
#else
    (void)token;
    (void)units;
#endif
}

#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
static bool dsp_aligned_16(const void *ptr)
{
    return ((uintptr_t)ptr & 0x0fU) == 0U;
}
#endif

static bool dsp_ranges_overlap(const void *a, size_t a_bytes,
                               const void *b, size_t b_bytes)
{
    if (a_bytes == 0U || b_bytes == 0U) {
        return false;
    }
    const uintptr_t a_start = (uintptr_t)a;
    const uintptr_t b_start = (uintptr_t)b;
    if (a_start <= b_start) {
        return b_start - a_start < a_bytes;
    }
    return a_start - b_start < b_bytes;
}

static bool dsp_partial_overlap(const void *dst, const void *src, size_t bytes)
{
    return dst != src && dsp_ranges_overlap(dst, bytes, src, bytes);
}

static int16_t dsp_saturate_s16(int64_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

/* Defined floor division avoids implementation-defined signed right shifts. */
static int64_t dsp_floor_div_pow2(int64_t value, unsigned shift)
{
    const int64_t divisor = (int64_t)1 << shift;
    if (value >= 0) {
        return value / divisor;
    }
    return -(((-value) + divisor - 1) / divisor);
}

static int16_t dsp_q15_product(int16_t a, int16_t b)
{
    return dsp_saturate_s16(dsp_floor_div_pow2((int64_t)a * b, 15U));
}

static uint64_t dsp_isqrt_u64(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = UINT64_C(1) << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0U) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

const char *solar_os_dsp_backend(void)
{
#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
    return "esp32s3-pie";
#else
    return "portable";
#endif
}

uint32_t solar_os_dsp_capabilities(void)
{
    return DSP_ALL_CAPABILITIES;
}

uint32_t solar_os_dsp_accelerated_capabilities(void)
{
#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
    return SOLAR_OS_DSP_CAP_GAIN_Q15 | SOLAR_OS_DSP_CAP_WINDOW_Q15 |
           SOLAR_OS_DSP_CAP_FFT_S16;
#else
    return 0U;
#endif
}

esp_err_t solar_os_dsp_dot_s16(const int16_t *a, const int16_t *b,
                               size_t count, int64_t *result)
{
    if (result == NULL || (count > 0U && (a == NULL || b == NULL))) {
        return ESP_ERR_INVALID_ARG;
    }
    dsp_engine_token_t token = dsp_engine_begin(false, "dot.s16");
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += (int64_t)a[i] * b[i];
    }
    *result = sum;
    dsp_engine_end(&token, count);
    return ESP_OK;
}

static bool dsp_can_accelerate_gain(const int16_t *dst, const int16_t *src,
                                    size_t count, int16_t gain_q15)
{
#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
    return count >= DSP_PIE_MIN_SAMPLES && gain_q15 != INT16_MIN &&
           dsp_aligned_16(dst) && dsp_aligned_16(src);
#else
    (void)dst;
    (void)src;
    (void)count;
    (void)gain_q15;
    return false;
#endif
}

esp_err_t solar_os_dsp_gain_q15(int16_t *dst, const int16_t *src,
                                size_t count, int16_t gain_q15)
{
    const size_t bytes = count * sizeof(*src);
    if ((count > 0U && (dst == NULL || src == NULL)) ||
        (count > SIZE_MAX / sizeof(*src)) ||
        dsp_partial_overlap(dst, src, bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool accelerated = dsp_can_accelerate_gain(dst, src, count, gain_q15);
    dsp_engine_token_t token = dsp_engine_begin(accelerated, "gain.q15");
    size_t done = 0;
#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
    if (accelerated) {
        int16_t gains[DSP_PIE_MIN_SAMPLES] __attribute__((aligned(16)));
        for (size_t i = 0; i < DSP_PIE_MIN_SAMPLES; i++) {
            gains[i] = gain_q15;
        }
        while (count - done >= DSP_PIE_MIN_SAMPLES) {
            (void)dsps_mul_s16_aes3(src + done, gains, dst + done,
                                     (int)DSP_PIE_MIN_SAMPLES, 1, 1, 1, 15);
            done += DSP_PIE_MIN_SAMPLES;
        }
    }
#endif
    for (size_t i = done; i < count; i++) {
        dst[i] = dsp_q15_product(src[i], gain_q15);
    }
    dsp_engine_end(&token, count);
    return ESP_OK;
}

esp_err_t solar_os_dsp_mix_q15(int16_t *dst, const int16_t *a,
                               const int16_t *b, size_t count,
                               int16_t gain_a_q15, int16_t gain_b_q15)
{
    if (count > SIZE_MAX / sizeof(*dst)) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t bytes = count * sizeof(*dst);
    if ((count > 0U && (dst == NULL || a == NULL || b == NULL)) ||
        dsp_partial_overlap(dst, a, bytes) || dsp_partial_overlap(dst, b, bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    dsp_engine_token_t token = dsp_engine_begin(false, "mix.q15");
    for (size_t i = 0; i < count; i++) {
        const int64_t sum = (int64_t)a[i] * gain_a_q15 +
                            (int64_t)b[i] * gain_b_q15;
        dst[i] = dsp_saturate_s16(dsp_floor_div_pow2(sum, 15U));
    }
    dsp_engine_end(&token, count);
    return ESP_OK;
}

esp_err_t solar_os_dsp_clip_s16(int16_t *dst, const int16_t *src,
                                size_t count, int16_t minimum, int16_t maximum)
{
    if (minimum > maximum || count > SIZE_MAX / sizeof(*src)) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t bytes = count * sizeof(*src);
    if ((count > 0U && (dst == NULL || src == NULL)) ||
        dsp_partial_overlap(dst, src, bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    dsp_engine_token_t token = dsp_engine_begin(false, "clip.s16");
    for (size_t i = 0; i < count; i++) {
        dst[i] = src[i] < minimum ? minimum :
                 src[i] > maximum ? maximum : src[i];
    }
    dsp_engine_end(&token, count);
    return ESP_OK;
}

esp_err_t solar_os_dsp_level_s16(const int16_t *src, size_t count,
                                 solar_os_dsp_level_t *level)
{
    if (level == NULL || count == 0U || src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    dsp_engine_token_t token = dsp_engine_begin(false, "level.s16");
    uint32_t peak = 0;
    uint64_t sum_squares = 0;
    for (size_t i = 0; i < count; i++) {
        const int32_t sample = src[i];
        const uint32_t magnitude = sample < 0 ? (uint32_t)-sample : (uint32_t)sample;
        if (magnitude > peak) {
            peak = magnitude;
        }
        sum_squares += (uint64_t)(sample * sample);
    }
    level->peak = peak;
    level->rms = (uint32_t)dsp_isqrt_u64(sum_squares / count);
    dsp_engine_end(&token, count);
    return ESP_OK;
}

static bool dsp_can_accelerate_window(const int16_t *dst, const int16_t *src,
                                      const int16_t *window, size_t count)
{
#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
    return count >= DSP_PIE_MIN_SAMPLES && dsp_aligned_16(dst) &&
           dsp_aligned_16(src) && dsp_aligned_16(window);
#else
    (void)dst;
    (void)src;
    (void)window;
    (void)count;
    return false;
#endif
}

esp_err_t solar_os_dsp_window_q15(int16_t *dst, const int16_t *src,
                                  const int16_t *window_q15, size_t count)
{
    if (count > SIZE_MAX / sizeof(*src)) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t bytes = count * sizeof(*src);
    if ((count > 0U && (dst == NULL || src == NULL || window_q15 == NULL)) ||
        dsp_partial_overlap(dst, src, bytes) ||
        dsp_ranges_overlap(dst, bytes, window_q15, bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool accelerated = dsp_can_accelerate_window(dst, src, window_q15, count);
    dsp_engine_token_t token = dsp_engine_begin(accelerated, "window.q15");
    size_t done = 0;
#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
    if (accelerated) {
        done = count & ~(DSP_PIE_LANES - 1U);
        (void)dsps_mul_s16_aes3(src, window_q15, dst, (int)done, 1, 1, 1, 15);
        for (size_t i = 0; i < done; i++) {
            if (src[i] == INT16_MIN && window_q15[i] == INT16_MIN) {
                dst[i] = INT16_MAX;
            }
        }
    }
#endif
    for (size_t i = done; i < count; i++) {
        dst[i] = dsp_q15_product(src[i], window_q15[i]);
    }
    dsp_engine_end(&token, count);
    return ESP_OK;
}

esp_err_t solar_os_dsp_fir_create(const int16_t *coefficients_q15,
                                  size_t taps, solar_os_dsp_fir_t **out_fir)
{
    if (out_fir == NULL || coefficients_q15 == NULL || taps == 0U ||
        taps > SOLAR_OS_DSP_MAX_TAPS) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_fir = NULL;
    solar_os_dsp_fir_t *fir = dsp_calloc(1, sizeof(*fir), "dsp.fir");
    if (fir == NULL) {
        return ESP_ERR_NO_MEM;
    }
    fir->coefficients = dsp_calloc(taps, sizeof(*fir->coefficients), "dsp.fir.coeff");
    fir->history = dsp_calloc(taps, sizeof(*fir->history), "dsp.fir.history");
    if (fir->coefficients == NULL || fir->history == NULL) {
        solar_os_dsp_fir_destroy(fir);
        return ESP_ERR_NO_MEM;
    }
    memcpy(fir->coefficients, coefficients_q15, taps * sizeof(*coefficients_q15));
    fir->taps = taps;
    *out_fir = fir;
    return ESP_OK;
}

void solar_os_dsp_fir_destroy(solar_os_dsp_fir_t *fir)
{
    if (fir == NULL) {
        return;
    }
    dsp_free(fir->history);
    dsp_free(fir->coefficients);
    dsp_free(fir);
}

void solar_os_dsp_fir_reset(solar_os_dsp_fir_t *fir)
{
    if (fir != NULL) {
        memset(fir->history, 0, fir->taps * sizeof(*fir->history));
        fir->position = 0;
    }
}

size_t solar_os_dsp_fir_taps(const solar_os_dsp_fir_t *fir)
{
    return fir != NULL ? fir->taps : 0U;
}

static int16_t dsp_fir_sample(solar_os_dsp_fir_t *fir, int16_t input)
{
    fir->history[fir->position] = input;
    size_t position = fir->position;
    int64_t accumulator = 0;
    for (size_t tap = 0; tap < fir->taps; tap++) {
        accumulator += (int64_t)fir->coefficients[tap] * fir->history[position];
        position = position == 0U ? fir->taps - 1U : position - 1U;
    }
    fir->position++;
    if (fir->position == fir->taps) {
        fir->position = 0;
    }
    return dsp_saturate_s16(dsp_floor_div_pow2(accumulator, 15U));
}

esp_err_t solar_os_dsp_fir_process(solar_os_dsp_fir_t *fir, int16_t *dst,
                                   const int16_t *src, size_t count)
{
    if (fir == NULL || count > SIZE_MAX / sizeof(*src)) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t bytes = count * sizeof(*src);
    if ((count > 0U && (dst == NULL || src == NULL)) ||
        dsp_partial_overlap(dst, src, bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    dsp_engine_token_t token = dsp_engine_begin(false, "fir.q15");
    for (size_t i = 0; i < count; i++) {
        dst[i] = dsp_fir_sample(fir, src[i]);
    }
    dsp_engine_end(&token, count);
    return ESP_OK;
}

esp_err_t solar_os_dsp_decimator_create(const int16_t *coefficients_q15,
                                        size_t taps, size_t factor,
                                        solar_os_dsp_decimator_t **out_decimator)
{
    if (out_decimator == NULL || factor < 2U || factor > UINT16_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_decimator = NULL;
    solar_os_dsp_decimator_t *decimator =
        dsp_calloc(1, sizeof(*decimator), "dsp.decimator");
    if (decimator == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t result = solar_os_dsp_fir_create(coefficients_q15, taps,
                                                &decimator->fir);
    if (result != ESP_OK) {
        dsp_free(decimator);
        return result;
    }
    decimator->factor = factor;
    *out_decimator = decimator;
    return ESP_OK;
}

void solar_os_dsp_decimator_destroy(solar_os_dsp_decimator_t *decimator)
{
    if (decimator != NULL) {
        solar_os_dsp_fir_destroy(decimator->fir);
        dsp_free(decimator);
    }
}

void solar_os_dsp_decimator_reset(solar_os_dsp_decimator_t *decimator)
{
    if (decimator != NULL) {
        solar_os_dsp_fir_reset(decimator->fir);
        decimator->phase = 0;
    }
}

size_t solar_os_dsp_decimator_factor(const solar_os_dsp_decimator_t *decimator)
{
    return decimator != NULL ? decimator->factor : 0U;
}

size_t solar_os_dsp_decimator_output_count(const solar_os_dsp_decimator_t *decimator,
                                           size_t input_count)
{
    if (decimator == NULL || input_count == 0U) {
        return 0U;
    }
    const size_t first = decimator->phase == 0U ? 0U :
                         decimator->factor - decimator->phase;
    if (first >= input_count) {
        return 0U;
    }
    return 1U + (input_count - 1U - first) / decimator->factor;
}

esp_err_t solar_os_dsp_decimator_process(solar_os_dsp_decimator_t *decimator,
                                         int16_t *dst, size_t dst_capacity,
                                         const int16_t *src, size_t src_count,
                                         size_t *produced)
{
    if (decimator == NULL || produced == NULL ||
        src_count > SIZE_MAX / sizeof(*src)) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t required = solar_os_dsp_decimator_output_count(decimator, src_count);
    if ((src_count > 0U && src == NULL) || (required > 0U && dst == NULL) ||
        dst_capacity < required ||
        dsp_ranges_overlap(dst, required * sizeof(*dst),
                           src, src_count * sizeof(*src))) {
        return dst_capacity < required ? ESP_ERR_INVALID_SIZE : ESP_ERR_INVALID_ARG;
    }
    dsp_engine_token_t token = dsp_engine_begin(false, "decimator.q15");
    size_t output_index = 0;
    for (size_t i = 0; i < src_count; i++) {
        const int16_t filtered = dsp_fir_sample(decimator->fir, src[i]);
        if (decimator->phase == 0U) {
            dst[output_index++] = filtered;
        }
        decimator->phase++;
        if (decimator->phase == decimator->factor) {
            decimator->phase = 0U;
        }
    }
    *produced = output_index;
    dsp_engine_end(&token, src_count);
    return ESP_OK;
}

static bool dsp_power_of_two(size_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static size_t dsp_reverse_bits(size_t value, uint8_t bits)
{
    size_t result = 0;
    for (uint8_t i = 0; i < bits; i++) {
        result = (result << 1U) | (value & 1U);
        value >>= 1U;
    }
    return result;
}

#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
static bool dsp_fft_accelerator_init(void)
{
    for (;;) {
        portENTER_CRITICAL(&dsp_fft_init_lock);
        if (dsp_fft_accelerator_ready || dsp_fft_init_attempted) {
            const bool ready = dsp_fft_accelerator_ready;
            portEXIT_CRITICAL(&dsp_fft_init_lock);
            return ready;
        }
        if (!dsp_fft_initializing) {
            dsp_fft_initializing = true;
            portEXIT_CRITICAL(&dsp_fft_init_lock);
            break;
        }
        portEXIT_CRITICAL(&dsp_fft_init_lock);
        vTaskDelay(1);
    }

    int16_t *table = solar_os_memory_calloc(
        CONFIG_DSP_MAX_FFT_SIZE,
        sizeof(*table),
        SOLAR_OS_MEMORY_EXTERNAL_PREFERRED,
        "dsp.fft.simd.table");
    esp_err_t err = table != NULL && dsp_aligned_16(table) ?
        dsps_fft2r_init_sc16(table, CONFIG_DSP_MAX_FFT_SIZE) : ESP_ERR_NO_MEM;
    if (err == ESP_OK && dsps_fft_w_table_sc16 != table) {
        solar_os_memory_free(table);
        table = NULL;
    }

    const bool ready = err == ESP_OK && dsps_fft_w_table_sc16 != NULL &&
                       dsps_fft_w_table_sc16_size >= CONFIG_DSP_MAX_FFT_SIZE &&
                       dsp_aligned_16(dsps_fft_w_table_sc16);
    if (!ready && table != NULL) {
        solar_os_memory_free(table);
        table = NULL;
    }

    portENTER_CRITICAL(&dsp_fft_init_lock);
    dsp_fft_owned_table = table;
    dsp_fft_accelerator_ready = ready;
    dsp_fft_init_attempted = true;
    dsp_fft_initializing = false;
    portEXIT_CRITICAL(&dsp_fft_init_lock);
    return ready;
}
#endif

esp_err_t solar_os_dsp_fft_create(size_t size, solar_os_dsp_fft_t **out_fft)
{
    if (out_fft == NULL || size < SOLAR_OS_DSP_FFT_MIN_SIZE ||
        size > SOLAR_OS_DSP_FFT_MAX_SIZE || !dsp_power_of_two(size)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_fft = NULL;
    solar_os_dsp_fft_t *fft = dsp_calloc(1, sizeof(*fft), "dsp.fft");
    if (fft == NULL) {
        return ESP_ERR_NO_MEM;
    }
    fft->twiddles = dsp_calloc(size / 2U, sizeof(*fft->twiddles), "dsp.fft.twiddle");
    const size_t work_bytes = size * sizeof(*fft->work);
    fft->work_allocation = dsp_calloc(1U, work_bytes + 15U, "dsp.fft.work");
    if (fft->work_allocation != NULL) {
        fft->work = (solar_os_dsp_complex_s16_t *)
            (((uintptr_t)fft->work_allocation + 15U) & ~(uintptr_t)0x0fU);
    }
    if (fft->twiddles == NULL || fft->work_allocation == NULL) {
        solar_os_dsp_fft_destroy(fft);
        return ESP_ERR_NO_MEM;
    }
    fft->size = size;
    for (size_t value = size; value > 1U; value >>= 1U) {
        fft->stages++;
    }
    for (size_t i = 0; i < size / 2U; i++) {
        const double angle = -2.0 * DSP_PI * (double)i / (double)size;
        fft->twiddles[i].real = (int16_t)lrint(cos(angle) * SOLAR_OS_DSP_Q15_ONE);
        fft->twiddles[i].imag = (int16_t)lrint(sin(angle) * SOLAR_OS_DSP_Q15_ONE);
    }
#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
    (void)dsp_fft_accelerator_init();
#endif
    *out_fft = fft;
    return ESP_OK;
}

void solar_os_dsp_fft_destroy(solar_os_dsp_fft_t *fft)
{
    if (fft != NULL) {
        dsp_free(fft->work_allocation);
        dsp_free(fft->twiddles);
        dsp_free(fft);
    }
}

size_t solar_os_dsp_fft_size(const solar_os_dsp_fft_t *fft)
{
    return fft != NULL ? fft->size : 0U;
}

esp_err_t solar_os_dsp_fft_execute(solar_os_dsp_fft_t *fft,
                                   solar_os_dsp_complex_s16_t *output,
                                   const int16_t *input,
                                   uint8_t *scale_exponent)
{
    if (fft == NULL || output == NULL || input == NULL || scale_exponent == NULL ||
        dsp_ranges_overlap(output, fft->size * sizeof(*output),
                           input, fft->size * sizeof(*input))) {
        return ESP_ERR_INVALID_ARG;
    }
    bool accelerated = false;
#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
    accelerated = fft->size >= DSP_PIE_MIN_SAMPLES &&
                  dsp_fft_accelerator_ready && dsp_aligned_16(fft->work);
#endif
    dsp_engine_token_t token = dsp_engine_begin(accelerated, "fft.s16");
#if SOLAR_OS_DSP_HAS_ESP32S3_PIE
    if (accelerated) {
        for (size_t i = 0; i < fft->size; i++) {
            fft->work[i].real = input[i];
            fft->work[i].imag = 0;
        }
        esp_err_t err = dsps_fft2r_sc16_aes3_(
            (int16_t *)fft->work,
            (int)fft->size,
            dsps_fft_w_table_sc16);
        if (err == ESP_OK) {
            err = dsps_bit_rev_sc16_ansi((int16_t *)fft->work,
                                         (int)fft->size);
        }
        if (err != ESP_OK) {
            dsp_engine_end(&token, fft->size);
            return err;
        }
        memcpy(output, fft->work, fft->size * sizeof(*output));
        *scale_exponent = fft->stages;
        dsp_engine_end(&token, fft->size);
        return ESP_OK;
    }
#endif
    for (size_t i = 0; i < fft->size; i++) {
        const size_t reversed = dsp_reverse_bits(i, fft->stages);
        fft->work[reversed].real = input[i];
        fft->work[reversed].imag = 0;
    }
    for (size_t length = 2U; length <= fft->size; length <<= 1U) {
        const size_t half = length / 2U;
        const size_t twiddle_step = fft->size / length;
        for (size_t base = 0; base < fft->size; base += length) {
            for (size_t j = 0; j < half; j++) {
                const solar_os_dsp_complex_s16_t u = fft->work[base + j];
                const solar_os_dsp_complex_s16_t v = fft->work[base + j + half];
                const solar_os_dsp_complex_s16_t w = fft->twiddles[j * twiddle_step];
                const int64_t tr_acc = (int64_t)v.real * w.real -
                                       (int64_t)v.imag * w.imag;
                const int64_t ti_acc = (int64_t)v.real * w.imag +
                                       (int64_t)v.imag * w.real;
                const int32_t tr = (int32_t)dsp_floor_div_pow2(tr_acc, 15U);
                const int32_t ti = (int32_t)dsp_floor_div_pow2(ti_acc, 15U);
                fft->work[base + j].real =
                    dsp_saturate_s16(dsp_floor_div_pow2((int64_t)u.real + tr, 1U));
                fft->work[base + j].imag =
                    dsp_saturate_s16(dsp_floor_div_pow2((int64_t)u.imag + ti, 1U));
                fft->work[base + j + half].real =
                    dsp_saturate_s16(dsp_floor_div_pow2((int64_t)u.real - tr, 1U));
                fft->work[base + j + half].imag =
                    dsp_saturate_s16(dsp_floor_div_pow2((int64_t)u.imag - ti, 1U));
            }
        }
    }
    memcpy(output, fft->work, fft->size * sizeof(*output));
    *scale_exponent = fft->stages;
    dsp_engine_end(&token, fft->size);
    return ESP_OK;
}
