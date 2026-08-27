// 查找和绘制 DSEG 字形，并刷新天气时钟的时分与秒 canvas。
#include "ui_dseg_clock.h"

#include "ui_text_format.h"
#include "ui_views.h"

namespace {
constexpr int kDecimalBase = 10;
constexpr size_t kHourMinuteTextSize = sizeof("00:00");
constexpr size_t kSecondTextSize = sizeof("00");
constexpr const char *kHourMinuteFormat = "%02d:%02d";

void format_two_digit_second_text(char out[kSecondTextSize], int second)
{
    out[0] = static_cast<char>('0' + second / kDecimalBase);
    out[1] = static_cast<char>('0' + second % kDecimalBase);
    out[2] = '\0';
}

void format_hour_minute_text(char out[kHourMinuteTextSize], const struct tm &local)
{
    if (!out) {
        return;
    }
    int written = snprintf(out, kHourMinuteTextSize, kHourMinuteFormat, local.tm_hour, local.tm_min);
    if (ui_text::format_failed(written, kHourMinuteTextSize)) {
        out[0] = '\0';
    }
}
}

const DsegGlyph *find_dseg_glyph(const DsegFont &font, char ch)
{
    if (!font.chars || !font.glyphs) {
        return nullptr;
    }
    const char *pos = strchr(font.chars, ch);
    if (!pos) {
        return nullptr;
    }
    return &font.glyphs[pos - font.chars];
}

int draw_dseg_text(lv_obj_t *canvas, const DsegFont &font, const char *text, int cursor_x, int baseline_y)
{
    int x_cursor = cursor_x;
    if (!canvas || !text || !font.bitmap) {
        return x_cursor;
    }
    for (const char *p = text; *p; ++p) {
        const DsegGlyph *glyph = find_dseg_glyph(font, *p);
        if (!glyph) {
            continue;
        }
        uint32_t bit = 0;
        for (int y = 0; y < glyph->height; ++y) {
            for (int x = 0; x < glyph->width; ++x, ++bit) {
                if (packed_1bit_bit_is_set(font.bitmap + glyph->bitmap_offset, bit)) {
                    lv_canvas_set_px_color(canvas,
                                           x_cursor + glyph->x_offset + x,
                                           baseline_y + glyph->y_offset + y,
                                           lv_color_black());
                }
            }
        }
        x_cursor += glyph->x_advance;
    }
    return x_cursor;
}

void draw_time_canvas(const struct tm &local)
{
    if (!g_time_canvas) {
        return;
    }
    lv_canvas_fill_bg(g_time_canvas, lv_color_white(), LV_OPA_COVER);

    char hm[kHourMinuteTextSize] = {};
    format_hour_minute_text(hm, local);
    draw_dseg_text(g_time_canvas, kDSEG84Font, hm, 0, 88);
    lv_obj_invalidate(g_time_canvas);
}

void draw_second_canvas(const struct tm &local)
{
    if (!g_second_canvas) {
        return;
    }
    lv_canvas_fill_bg(g_second_canvas, lv_color_white(), LV_OPA_COVER);
    char ss[kSecondTextSize] = {};
    format_two_digit_second_text(ss, local.tm_sec);
    draw_dseg_text(g_second_canvas, kDSEG36Font, ss, 0, 40);
    lv_obj_invalidate(g_second_canvas);
}
