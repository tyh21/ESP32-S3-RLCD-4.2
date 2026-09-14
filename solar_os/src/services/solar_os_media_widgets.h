#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_gfx.h"

typedef struct solar_os_cassette_widget solar_os_cassette_widget_t;

typedef enum {
    SOLAR_OS_MEDIA_TRANSPORT_PREVIOUS,
    SOLAR_OS_MEDIA_TRANSPORT_PLAY,
    SOLAR_OS_MEDIA_TRANSPORT_PAUSE,
    SOLAR_OS_MEDIA_TRANSPORT_STOP,
    SOLAR_OS_MEDIA_TRANSPORT_RECORD,
    SOLAR_OS_MEDIA_TRANSPORT_NEXT,
} solar_os_media_transport_icon_t;

void solar_os_media_transport_button_draw(
    solar_os_gfx_t *gfx,
    int x,
    int y,
    int width,
    int height,
    solar_os_media_transport_icon_t icon,
    bool active);

esp_err_t solar_os_cassette_widget_create(solar_os_cassette_widget_t **widget);
void solar_os_cassette_widget_destroy(solar_os_cassette_widget_t *widget);
void solar_os_cassette_widget_reset(solar_os_cassette_widget_t *widget);
void solar_os_cassette_widget_update(solar_os_cassette_widget_t *widget,
                                     bool playing,
                                     uint32_t elapsed_ms,
                                     uint32_t total_ms,
                                     uint32_t now_ms);
void solar_os_cassette_widget_draw(solar_os_cassette_widget_t *widget,
                                   solar_os_gfx_t *gfx,
                                   int x,
                                   int y,
                                   int width,
                                   int height);
