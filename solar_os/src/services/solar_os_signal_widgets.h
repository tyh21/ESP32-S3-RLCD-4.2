#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_gfx.h"

typedef struct solar_os_oscilloscope_widget solar_os_oscilloscope_widget_t;
typedef struct solar_os_spectrum_widget solar_os_spectrum_widget_t;

esp_err_t solar_os_oscilloscope_widget_create(
    size_t sample_capacity,
    solar_os_oscilloscope_widget_t **out_widget);
void solar_os_oscilloscope_widget_destroy(
    solar_os_oscilloscope_widget_t *widget);
void solar_os_oscilloscope_widget_reset(
    solar_os_oscilloscope_widget_t *widget);
esp_err_t solar_os_oscilloscope_widget_submit_s16(
    solar_os_oscilloscope_widget_t *widget,
    const int16_t *samples,
    size_t frames,
    uint8_t channels);
void solar_os_oscilloscope_widget_draw(
    solar_os_oscilloscope_widget_t *widget,
    solar_os_gfx_t *gfx,
    int x,
    int y,
    int width,
    int height);

esp_err_t solar_os_spectrum_widget_create(
    size_t fft_size,
    solar_os_spectrum_widget_t **out_widget);
void solar_os_spectrum_widget_destroy(solar_os_spectrum_widget_t *widget);
void solar_os_spectrum_widget_reset(solar_os_spectrum_widget_t *widget);
esp_err_t solar_os_spectrum_widget_submit_s16(
    solar_os_spectrum_widget_t *widget,
    const int16_t *samples,
    size_t frames,
    uint8_t channels);
void solar_os_spectrum_widget_draw(
    solar_os_spectrum_widget_t *widget,
    solar_os_gfx_t *gfx,
    int x,
    int y,
    int width,
    int height);
