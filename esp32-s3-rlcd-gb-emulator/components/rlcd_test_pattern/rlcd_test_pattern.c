#include "rlcd_test_pattern.h"

#include "board_rlcd.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rlcd_test_pattern";

esp_err_t rlcd_test_pattern_draw_basic(void)
{
    const uint16_t width = board_rlcd_get_logical_width();
    const uint16_t height = board_rlcd_get_logical_height();

    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_WHITE), TAG, "clear failed");

    for (uint16_t x = 0; x < width; x++) {
        ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(x, 0, BOARD_RLCD_COLOR_BLACK), TAG, "set pixel failed");
        ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(x, height - 1, BOARD_RLCD_COLOR_BLACK),
                            TAG,
                            "set pixel failed");
    }

    for (uint16_t y = 0; y < height; y++) {
        ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(0, y, BOARD_RLCD_COLOR_BLACK), TAG, "set pixel failed");
        ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(width - 1, y, BOARD_RLCD_COLOR_BLACK),
                            TAG,
                            "set pixel failed");
    }

    for (uint16_t x = 0; x < width; x++) {
        uint16_t y1 = (uint16_t)((uint32_t)x * height / width);
        uint16_t y2 = height - 1 - y1;
        ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(x, y1, BOARD_RLCD_COLOR_BLACK), TAG, "set pixel failed");
        ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(x, y2, BOARD_RLCD_COLOR_BLACK), TAG, "set pixel failed");
    }

    for (uint16_t x = 20; x + 1 < width; x += 40) {
        for (uint16_t y = 20; y < height - 20; y++) {
            ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(x, y, BOARD_RLCD_COLOR_BLACK), TAG, "set pixel failed");
            ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(x + 1, y, BOARD_RLCD_COLOR_BLACK), TAG, "set pixel failed");
        }
    }

    return board_rlcd_flush();
}

static esp_err_t rlcd_test_pattern_draw_hline(uint16_t x, uint16_t y, uint16_t w, board_rlcd_color_t color)
{
    for (uint16_t i = 0; i < w; i++) {
        ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(x + i, y, color), TAG, "set pixel failed");
    }

    return ESP_OK;
}

static esp_err_t rlcd_test_pattern_draw_vline(uint16_t x, uint16_t y, uint16_t h, board_rlcd_color_t color)
{
    for (uint16_t i = 0; i < h; i++) {
        ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(x, y + i, color), TAG, "set pixel failed");
    }

    return ESP_OK;
}

static esp_err_t rlcd_test_pattern_fill_rect(uint16_t x,
                                             uint16_t y,
                                             uint16_t w,
                                             uint16_t h,
                                             board_rlcd_color_t color)
{
    for (uint16_t row = 0; row < h; row++) {
        ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_hline(x, y + row, w, color), TAG, "draw hline failed");
    }

    return ESP_OK;
}

static esp_err_t rlcd_test_pattern_draw_rect(uint16_t x,
                                             uint16_t y,
                                             uint16_t w,
                                             uint16_t h,
                                             board_rlcd_color_t color)
{
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_hline(x, y, w, color), TAG, "draw hline failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_hline(x, y + h - 1, w, color), TAG, "draw hline failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_vline(x, y, h, color), TAG, "draw vline failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_vline(x + w - 1, y, h, color), TAG, "draw vline failed");

    return ESP_OK;
}

static const uint8_t *rlcd_test_pattern_get_5x7_glyph(char ch)
{
    static const uint8_t glyph_0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t glyph_1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t glyph_2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t glyph_3[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t glyph_4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t glyph_a[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t glyph_b[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t glyph_e[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t glyph_f[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t glyph_g[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
    static const uint8_t glyph_h[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t glyph_i[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t glyph_l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t glyph_m[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t glyph_o[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_p[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t glyph_r[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t glyph_t[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t glyph_x[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t glyph_y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t glyph_plus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    static const uint8_t glyph_space[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    switch (ch) {
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case 'A': return glyph_a;
    case 'B': return glyph_b;
    case 'E': return glyph_e;
    case 'F': return glyph_f;
    case 'G': return glyph_g;
    case 'H': return glyph_h;
    case 'I': return glyph_i;
    case 'L': return glyph_l;
    case 'M': return glyph_m;
    case 'O': return glyph_o;
    case 'P': return glyph_p;
    case 'R': return glyph_r;
    case 'T': return glyph_t;
    case 'X': return glyph_x;
    case 'Y': return glyph_y;
    case '+': return glyph_plus;
    case ' ': return glyph_space;
    default: return glyph_space;
    }
}

static esp_err_t rlcd_test_pattern_draw_char_5x7(uint16_t x,
                                                 uint16_t y,
                                                 char ch,
                                                 uint8_t scale,
                                                 board_rlcd_color_t color)
{
    const uint8_t *glyph = rlcd_test_pattern_get_5x7_glyph(ch);

    for (uint8_t row = 0; row < 7; row++) {
        for (uint8_t col = 0; col < 5; col++) {
            if ((glyph[row] & (1U << (4 - col))) != 0) {
                ESP_RETURN_ON_ERROR(rlcd_test_pattern_fill_rect((uint16_t)(x + col * scale),
                                                                (uint16_t)(y + row * scale),
                                                                scale,
                                                                scale,
                                                                color),
                                    TAG,
                                    "fill glyph pixel failed");
            }
        }
    }

    return ESP_OK;
}

static esp_err_t rlcd_test_pattern_draw_text(uint16_t x,
                                             uint16_t y,
                                             const char *text,
                                             uint8_t scale,
                                             board_rlcd_color_t color)
{
    uint16_t cursor_x = x;

    while (*text != '\0') {
        ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_char_5x7(cursor_x, y, *text, scale, color),
                            TAG,
                            "draw char failed");
        cursor_x = (uint16_t)(cursor_x + 6 * scale);
        text++;
    }

    return ESP_OK;
}

static uint16_t rlcd_test_pattern_text_width(const char *text, uint8_t scale)
{
    uint16_t count = 0;
    while (text[count] != '\0') {
        count++;
    }

    return (uint16_t)(count * 6 * scale);
}

static uint16_t rlcd_test_pattern_center_x(uint16_t width, const char *text, uint8_t scale)
{
    uint16_t text_width = rlcd_test_pattern_text_width(text, scale);
    if (text_width >= width) {
        return 0;
    }

    return (uint16_t)((width - text_width) / 2);
}

esp_err_t rlcd_test_pattern_draw_orientation(void)
{
    const uint16_t width = board_rlcd_get_logical_width();
    const uint16_t height = board_rlcd_get_logical_height();
    const uint16_t center_y = height / 2;

    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_WHITE), TAG, "clear failed");

    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_rect(0, 0, width, height, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw outer border failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_rect(8, 8, width - 16, height - 16, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw inner border failed");

    ESP_RETURN_ON_ERROR(rlcd_test_pattern_fill_rect(18, 18, 18, 18, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw corner mark failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_fill_rect(width - 46, 18, 28, 18, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw corner mark failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_fill_rect(18, height - 46, 18, 28, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw corner mark failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_fill_rect(width - 56, height - 56, 38, 38, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw corner mark failed");

    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(46, 18, "TL", 3, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(width - 92, 18, "TR", 3, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(46, height - 46, "BL", 3, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(width - 102, height - 46, "BR", 3, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");

    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(rlcd_test_pattern_center_x(width, "TOP", 3),
                                                    22,
                                                    "TOP",
                                                    3,
                                                    BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(rlcd_test_pattern_center_x(width, "BOTTOM", 2),
                                                    height - 44,
                                                    "BOTTOM",
                                                    2,
                                                    BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(18, (uint16_t)(center_y - 18), "LEFT", 2, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(width - 78,
                                                    (uint16_t)(center_y - 18),
                                                    "RIGHT",
                                                    2,
                                                    BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");

    const uint16_t x_axis_start = width / 5;
    const uint16_t x_axis_y = height / 3;
    const uint16_t x_axis_len = (uint16_t)(width * 3 / 5);
    const uint16_t x_arrow_x = (uint16_t)(x_axis_start + x_axis_len);
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_hline(x_axis_start, x_axis_y, x_axis_len, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw x axis failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_hline(x_axis_start, (uint16_t)(x_axis_y + 1), x_axis_len, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw x axis failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_fill_rect(x_arrow_x, (uint16_t)(x_axis_y - 7), 14, 14, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw x arrow failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text((uint16_t)(x_arrow_x + 18),
                                                    (uint16_t)(x_axis_y - 11),
                                                    "X+",
                                                    2,
                                                    BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");

    const uint16_t y_axis_x = width / 5;
    const uint16_t y_axis_start = height / 3;
    const uint16_t y_axis_len = height / 3;
    const uint16_t y_arrow_y = (uint16_t)(y_axis_start + y_axis_len);
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_vline(y_axis_x, y_axis_start, y_axis_len, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw y axis failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_vline((uint16_t)(y_axis_x + 1), y_axis_start, y_axis_len, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw y axis failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_fill_rect((uint16_t)(y_axis_x - 6), y_arrow_y, 14, 14, BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw y arrow failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text((uint16_t)(y_axis_x + 18),
                                                    (uint16_t)(y_arrow_y - 6),
                                                    "Y+",
                                                    2,
                                                    BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");

    for (uint16_t x = (uint16_t)(x_axis_start + 30); x < x_arrow_x; x += 40) {
        ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_vline(x, (uint16_t)(x_axis_y - 6), 12, BOARD_RLCD_COLOR_BLACK),
                            TAG,
                            "draw x tick failed");
    }
    for (uint16_t y = (uint16_t)(y_axis_start + 30); y < y_arrow_y; y += 40) {
        ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_hline((uint16_t)(y_axis_x - 6), y, 12, BOARD_RLCD_COLOR_BLACK),
                            TAG,
                            "draw y tick failed");
    }

    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(rlcd_test_pattern_center_x(width, "ST7305", 3),
                                                    (uint16_t)(center_y - 24),
                                                    "ST7305",
                                                    3,
                                                    BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");
    ESP_RETURN_ON_ERROR(rlcd_test_pattern_draw_text(rlcd_test_pattern_center_x(width, "400X300", 2),
                                                    (uint16_t)(center_y + 14),
                                                    "400X300",
                                                    2,
                                                    BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw text failed");

    return board_rlcd_flush();
}

esp_err_t rlcd_test_pattern_benchmark_flush(uint32_t duration_ms)
{
    if (!board_rlcd_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int64_t test_time_us = (int64_t)duration_ms * 1000;
    const int64_t start_us = esp_timer_get_time();
    int64_t now_us = start_us;
    uint32_t frames = 0;

    ESP_LOGI(TAG, "Starting RLCD full-screen flush benchmark for %lu ms", duration_ms);

    while ((now_us - start_us) < test_time_us) {
        ESP_RETURN_ON_ERROR(board_rlcd_clear((frames & 1U) ? BOARD_RLCD_COLOR_BLACK : BOARD_RLCD_COLOR_WHITE),
                            TAG,
                            "clear failed during benchmark");
        ESP_RETURN_ON_ERROR(board_rlcd_flush(), TAG, "flush failed during benchmark");
        frames++;
        now_us = esp_timer_get_time();
    }

    const int64_t elapsed_us = now_us - start_us;
    const double fps = (double)frames * 1000000.0 / (double)elapsed_us;
    const double frame_ms = (double)elapsed_us / 1000.0 / (double)frames;

    ESP_LOGI(TAG,
             "RLCD benchmark result: frames=%lu elapsed=%.2f ms fps=%.2f frame_time=%.2f ms",
             frames,
             (double)elapsed_us / 1000.0,
             fps,
             frame_ms);

    return ESP_OK;
}
