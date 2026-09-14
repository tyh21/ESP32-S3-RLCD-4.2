#include "solar_os_media_widgets.h"

#include <string.h>

#include "solar_os_memory.h"

struct solar_os_cassette_widget {
    uint8_t phase;
    bool playing;
    uint32_t elapsed_ms;
    uint32_t total_ms;
    uint32_t last_tick_ms;
};

static void media_transport_triangle(solar_os_gfx_t *gfx,
                                     int left,
                                     int top,
                                     int size,
                                     bool points_right)
{
    const solar_os_gfx_point_t points[] = {
        {points_right ? left : left + size, top},
        {points_right ? left : left + size, top + size},
        {points_right ? left + size : left, top + size / 2},
    };
    solar_os_gfx_fill_polygon(gfx, points, 3U);
}

void solar_os_media_transport_button_draw(
    solar_os_gfx_t *gfx,
    int x,
    int y,
    int width,
    int height,
    solar_os_media_transport_icon_t icon,
    bool active)
{
    if (gfx == NULL || width < 12 || height < 12) {
        return;
    }
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
    if (active) {
        solar_os_gfx_fill_rect(gfx, x, y, width, height);
        solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_WHITE);
    } else {
        solar_os_gfx_rect(gfx, x, y, width, height);
    }

    int size = height - 10;
    if (size > width - 12) size = width - 12;
    if (size < 6) size = 6;
    const int cx = x + width / 2;
    const int cy = y + height / 2;
    const int left = cx - size / 2;
    const int top = cy - size / 2;
    const int bar = size / 4 > 2 ? size / 4 : 2;

    switch (icon) {
    case SOLAR_OS_MEDIA_TRANSPORT_PREVIOUS:
        solar_os_gfx_fill_rect(gfx, left, top, bar, size);
        media_transport_triangle(gfx, left + bar + 1, top, size, false);
        break;
    case SOLAR_OS_MEDIA_TRANSPORT_PLAY:
        media_transport_triangle(gfx, left, top, size, true);
        break;
    case SOLAR_OS_MEDIA_TRANSPORT_PAUSE:
        solar_os_gfx_fill_rect(gfx, left, top, bar, size);
        solar_os_gfx_fill_rect(gfx, left + size - bar, top, bar, size);
        break;
    case SOLAR_OS_MEDIA_TRANSPORT_STOP:
        solar_os_gfx_fill_rect(gfx, left, top, size, size);
        break;
    case SOLAR_OS_MEDIA_TRANSPORT_RECORD:
        solar_os_gfx_fill_circle(gfx, cx, cy, size / 2);
        break;
    case SOLAR_OS_MEDIA_TRANSPORT_NEXT:
        media_transport_triangle(gfx, left - bar - 1, top, size, true);
        solar_os_gfx_fill_rect(gfx, left + size, top, bar, size);
        break;
    default:
        break;
    }
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
}

esp_err_t solar_os_cassette_widget_create(solar_os_cassette_widget_t **out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = solar_os_memory_calloc(1U, sizeof(**out),
                                  SOLAR_OS_MEMORY_EXTERNAL_PREFERRED,
                                  "widget.cassette");
    return *out != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

void solar_os_cassette_widget_destroy(solar_os_cassette_widget_t *widget)
{
    solar_os_memory_free(widget);
}

void solar_os_cassette_widget_reset(solar_os_cassette_widget_t *widget)
{
    if (widget != NULL) {
        memset(widget, 0, sizeof(*widget));
    }
}

void solar_os_cassette_widget_update(solar_os_cassette_widget_t *widget,
                                     bool playing,
                                     uint32_t elapsed_ms,
                                     uint32_t total_ms,
                                     uint32_t now_ms)
{
    if (widget == NULL) {
        return;
    }
    if (playing && (!widget->playing || now_ms - widget->last_tick_ms >= 90U)) {
        widget->phase = (uint8_t)((widget->phase + 1U) & 7U);
        widget->last_tick_ms = now_ms;
    }
    widget->playing = playing;
    widget->elapsed_ms = elapsed_ms;
    widget->total_ms = total_ms;
}

static void cassette_spokes(solar_os_gfx_t *gfx, int x, int y, int radius,
                            uint8_t phase)
{
    static const int8_t directions[8][2] = {
        {0, -8}, {6, -6}, {8, 0}, {6, 6},
        {0, 8}, {-6, 6}, {-8, 0}, {-6, -6},
    };
    for (uint8_t i = 0U; i < 4U; i++) {
        const uint8_t direction = (uint8_t)((phase + i * 2U) & 7U);
        solar_os_gfx_line(gfx, x, y,
                          x + directions[direction][0] * radius / 8,
                          y + directions[direction][1] * radius / 8);
    }
}

static void cassette_fill_capsule(solar_os_gfx_t *gfx,
                                  int x,
                                  int y,
                                  int width,
                                  int height)
{
    const int radius = height / 2;
    if (width <= height || radius <= 0) {
        solar_os_gfx_fill_rect(gfx, x, y, width, height);
        return;
    }
    solar_os_gfx_fill_rect(gfx, x + radius, y, width - height, height);
    solar_os_gfx_fill_circle(gfx, x + radius, y + radius, radius);
    solar_os_gfx_fill_circle(gfx, x + width - radius - 1, y + radius, radius);
}

static void cassette_draw_reel(solar_os_gfx_t *gfx,
                               int x,
                               int y,
                               int radius,
                               uint8_t phase)
{
    static const int8_t directions[8][2] = {
        {0, -8}, {6, -6}, {8, 0}, {6, 6},
        {0, 8}, {-6, 6}, {-8, 0}, {-6, -6},
    };

    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_WHITE);
    solar_os_gfx_fill_circle(gfx, x, y, radius);

    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
    const int tooth_radius = radius > 11 ? 2 : 1;
    const int tooth_distance = radius - tooth_radius;
    /* Four cut-outs make a 45-degree phase change visible.  Eight identical
     * cut-outs occupied every phase position and therefore looked stationary. */
    for (uint8_t i = 0U; i < 4U; i++) {
        const uint8_t direction = (uint8_t)((phase + i * 2U) & 7U);
        const int tx = x + directions[direction][0] * tooth_distance / 8;
        const int ty = y + directions[direction][1] * tooth_distance / 8;
        solar_os_gfx_fill_circle(gfx, tx, ty, tooth_radius);
    }
    solar_os_gfx_fill_circle(gfx, x, y, radius / 3);

    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_WHITE);
    cassette_spokes(gfx, x, y, radius / 3, phase);
}

void solar_os_cassette_widget_draw(solar_os_cassette_widget_t *widget,
                                   solar_os_gfx_t *gfx,
                                   int x,
                                   int y,
                                   int width,
                                   int height)
{
    if (widget == NULL || gfx == NULL || width < 80 || height < 50) {
        return;
    }
    int cassette_height = height * 9 / 10;
    int cassette_width = cassette_height * 3 / 2;
    if (cassette_width > width * 4 / 5) {
        cassette_width = width * 4 / 5;
        cassette_height = cassette_width * 2 / 3;
    }
    const int left = x + (width - cassette_width) / 2;
    const int top = y + (height - cassette_height) / 2;
    const int corner = cassette_height / 24 > 3 ? cassette_height / 24 : 3;
    const int window_x = left + cassette_width * 13 / 100;
    const int window_y = top + cassette_height * 27 / 100;
    const int window_width = cassette_width * 74 / 100;
    const int window_height = cassette_height * 36 / 100;
    const int cy = window_y + window_height / 2;
    const int left_hub = left + cassette_width * 29 / 100;
    const int right_hub = left + cassette_width * 71 / 100;
    const int maximum = window_height * 38 / 100;
    const int minimum = maximum * 4 / 5;
    uint32_t progress = 500U;
    if (widget->total_ms > 0U) {
        progress = widget->elapsed_ms >= widget->total_ms ? 1000U :
            (uint32_t)(((uint64_t)widget->elapsed_ms * 1000U) /
                       widget->total_ms);
    }
    const int supply = maximum - (maximum - minimum) * (int)progress / 1000;
    const int takeup = minimum + (maximum - minimum) * (int)progress / 1000;

    /* Solid shell with the softly clipped corners of the reference cassette. */
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
    solar_os_gfx_fill_rect(gfx, left + corner, top,
                           cassette_width - 2 * corner, cassette_height);
    solar_os_gfx_fill_rect(gfx, left, top + corner,
                           cassette_width, cassette_height - 2 * corner);
    solar_os_gfx_fill_circle(gfx, left + corner, top + corner, corner);
    solar_os_gfx_fill_circle(gfx, left + cassette_width - corner - 1,
                             top + corner, corner);
    solar_os_gfx_fill_circle(gfx, left + corner,
                             top + cassette_height - corner - 1, corner);
    solar_os_gfx_fill_circle(gfx, left + cassette_width - corner - 1,
                             top + cassette_height - corner - 1, corner);

    /* Four recessed shell screws. */
    const int screw_inset = corner + 1;
    const int screw_radius = corner > 4 ? 2 : 1;
    const int screw_x[2] = {left + screw_inset,
                            left + cassette_width - screw_inset - 1};
    const int screw_y[2] = {top + screw_inset,
                            top + cassette_height - screw_inset - 1};
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_WHITE);
    for (size_t sy = 0U; sy < 2U; sy++) {
        for (size_t sx = 0U; sx < 2U; sx++) {
            solar_os_gfx_fill_circle(gfx, screw_x[sx], screw_y[sy],
                                     screw_radius);
        }
    }

    /* White-edged tape window, dark interior, reels and center apertures. */
    cassette_fill_capsule(gfx, window_x, window_y,
                          window_width, window_height);
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
    cassette_fill_capsule(gfx, window_x + 2, window_y + 2,
                          window_width - 4, window_height - 4);

    const int bridge_x = left_hub + maximum;
    const int bridge_width = right_hub - left_hub - 2 * maximum;
    if (bridge_width > 6) {
        solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_WHITE);
        solar_os_gfx_fill_rect(gfx, bridge_x, cy - maximum / 2,
                               bridge_width, maximum);
        solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
        const int split = bridge_width / 7 > 2 ? bridge_width / 7 : 2;
        solar_os_gfx_fill_rect(gfx, bridge_x + 2, cy - maximum / 2 + 2,
                               (bridge_width - split - 4) / 2,
                               maximum - 4);
        solar_os_gfx_fill_rect(gfx,
                               bridge_x + (bridge_width + split) / 2,
                               cy - maximum / 2 + 2,
                               (bridge_width - split - 4) / 2,
                               maximum - 4);
    }
    cassette_draw_reel(gfx, left_hub, cy, supply, widget->phase);
    cassette_draw_reel(gfx, right_hub, cy, takeup, widget->phase);

    /* Lower tape-guide plate and its five characteristic openings. */
    const int plate_top = top + cassette_height * 82 / 100;
    const solar_os_gfx_point_t outer_plate[] = {
        {left + cassette_width * 27 / 100, top + cassette_height - 1},
        {left + cassette_width * 30 / 100, plate_top},
        {left + cassette_width * 70 / 100, plate_top},
        {left + cassette_width * 74 / 100, top + cassette_height - 1},
    };
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_WHITE);
    solar_os_gfx_fill_polygon(gfx, outer_plate,
                              sizeof(outer_plate) / sizeof(outer_plate[0]));
    const solar_os_gfx_point_t inner_plate[] = {
        {left + cassette_width * 29 / 100, top + cassette_height - 1},
        {left + cassette_width * 32 / 100, plate_top + 2},
        {left + cassette_width * 68 / 100, plate_top + 2},
        {left + cassette_width * 72 / 100, top + cassette_height - 1},
    };
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
    solar_os_gfx_fill_polygon(gfx, inner_plate,
                              sizeof(inner_plate) / sizeof(inner_plate[0]));

    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_WHITE);
    const int hole_radius = cassette_height / 34 > 1 ?
        cassette_height / 34 : 2;
    solar_os_gfx_fill_circle(gfx, left + cassette_width * 34 / 100,
                             plate_top + cassette_height * 9 / 100,
                             hole_radius);
    solar_os_gfx_fill_circle(gfx, left + cassette_width * 66 / 100,
                             plate_top + cassette_height * 9 / 100,
                             hole_radius);
    solar_os_gfx_fill_circle(gfx, left + cassette_width / 2,
                             plate_top + cassette_height * 4 / 100,
                             hole_radius > 2 ? 2 : 1);
    const int square = hole_radius * 2;
    solar_os_gfx_fill_rect(gfx, left + cassette_width * 40 / 100,
                           plate_top + cassette_height * 8 / 100,
                           square, square);
    solar_os_gfx_fill_rect(gfx, left + cassette_width * 58 / 100 - square,
                           plate_top + cassette_height * 8 / 100,
                           square, square);
}
