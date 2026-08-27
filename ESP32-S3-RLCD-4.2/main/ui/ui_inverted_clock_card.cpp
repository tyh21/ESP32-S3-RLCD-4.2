// 绘制反显 DSEG 数字牌的黑底、缩放数字和圆角裁切像素。
#include "ui_inverted_clock_card.h"

#include "ui_views.h"
#include "ui_dseg_layout.h"

#define INVERTED_CLOCK_CARD_CANVAS_CREATE_FAILED_FORMAT "flip clock card %d canvas create failed"

namespace inverted_clock_card {
namespace {

constexpr int kRadius = 8;
constexpr int kDigitScaleNumerator = 3;
constexpr int kDigitScaleDenominator = 4;
constexpr int kDigitBaselineY = 84;

static_assert(kRadius > 0 && kRadius * 2 <= kWidth && kRadius * 2 <= kHeight,
              "inverted clock card radius must fit card");
static_assert(kDigitScaleNumerator > 0 && kDigitScaleDenominator > 0,
              "inverted clock digit scale must be positive");
static_assert(kCount == 3, "inverted clock card group must keep three cards");
static_assert(kX[0] >= 0 && kX[kCount - 1] + kWidth <= kDisplayWidth,
              "inverted clock card group must fit display width");
static_assert(kY >= 0 && kY + kHeight <= kDisplayHeight,
              "inverted clock card group must fit display height");

void apply_rounding(lv_obj_t *canvas)
{
    if (!canvas) {
        return;
    }
    int r2 = kRadius * kRadius;
    for (int y = 0; y < kRadius; ++y) {
        for (int x = 0; x < kRadius; ++x) {
            int dx = kRadius - 1 - x;
            int dy = kRadius - 1 - y;
            if (dx * dx + dy * dy > r2) {
                canvas_set_px_safe(canvas, x, y, kWidth, kHeight, lv_color_white());
                canvas_set_px_safe(canvas, kWidth - 1 - x, y, kWidth, kHeight, lv_color_white());
                canvas_set_px_safe(canvas, x, kHeight - 1 - y, kWidth, kHeight, lv_color_white());
                canvas_set_px_safe(canvas,
                                   kWidth - 1 - x,
                                   kHeight - 1 - y,
                                   kWidth,
                                   kHeight,
                                   lv_color_white());
            }
        }
    }
}

bool dseg_pixel_on(const DsegFont &font, const DsegGlyph *glyph, int x, int y)
{
    uint32_t bit = static_cast<uint32_t>(y) * glyph->width + x;
    return packed_1bit_bit_is_set(font.bitmap + glyph->bitmap_offset, bit);
}

void draw_scaled_dseg_digit(lv_obj_t *canvas,
                            const DsegGlyph *glyph,
                            int origin_x,
                            int origin_y,
                            int scale_num,
                            int scale_den,
                            int clip_y0 = 0,
                            int clip_y1 = kHeight)
{
    if (!canvas || !glyph || scale_num <= 0 || scale_den <= 0) {
        return;
    }
    int dst_w = (glyph->width * scale_num + scale_den - 1) / scale_den;
    int dst_h = (glyph->height * scale_num + scale_den - 1) / scale_den;
    int dst_x = origin_x + (glyph->x_offset * scale_num) / scale_den;
    int dst_y = origin_y + (glyph->y_offset * scale_num) / scale_den;
    for (int y = 0; y < dst_h; ++y) {
        int src_y = (y * glyph->height) / dst_h;
        for (int x = 0; x < dst_w; ++x) {
            int src_x = (x * glyph->width) / dst_w;
            int py = dst_y + y;
            if (py >= clip_y0 && py < clip_y1 &&
                dseg_pixel_on(kDSEG84Font, glyph, src_x, src_y)) {
                canvas_set_px_safe(canvas,
                                   dst_x + x,
                                   dst_y + y,
                                   kWidth,
                                   kHeight,
                                   lv_color_white());
            }
        }
    }
}

void draw_digits(lv_obj_t *canvas, int value, int clip_y0 = 0, int clip_y1 = kHeight)
{
    const DsegGlyph *tens = find_dseg_glyph(kDSEG84Font, static_cast<char>('0' + value / 10));
    const DsegGlyph *ones = find_dseg_glyph(kDSEG84Font, static_cast<char>('0' + value % 10));
    if (!tens || !ones) {
        return;
    }
    DsegPairLayout layout = centered_dseg_pair_layout(kWidth,
                                                       kDigitScaleNumerator,
                                                       kDigitScaleDenominator,
                                                       tens->x_offset,
                                                       tens->width,
                                                       tens->x_advance,
                                                       ones->x_offset,
                                                       ones->width);
    draw_scaled_dseg_digit(canvas,
                           tens,
                           layout.first_origin_x,
                           kDigitBaselineY,
                           kDigitScaleNumerator,
                           kDigitScaleDenominator,
                           clip_y0,
                           clip_y1);
    draw_scaled_dseg_digit(canvas,
                           ones,
                           layout.second_origin_x,
                           kDigitBaselineY,
                           kDigitScaleNumerator,
                           kDigitScaleDenominator,
                           clip_y0,
                           clip_y1);
}

void draw_shell(lv_obj_t *canvas)
{
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
}

} // namespace

void render(lv_obj_t *canvas, int value)
{
    if (!canvas) {
        return;
    }
    draw_shell(canvas);
    draw_digits(canvas, value);
    apply_rounding(canvas);
    lv_obj_invalidate(canvas);
}

void clear(lv_obj_t *canvas)
{
    if (!canvas) {
        return;
    }
    draw_shell(canvas);
    apply_rounding(canvas);
    lv_obj_invalidate(canvas);
}

} // namespace inverted_clock_card

void build_inverted_clock_cards(lv_obj_t *parent,
                                lv_obj_t *card_canvas[3],
                                lv_color_t *card_canvas_buf[3])
{
    if (!parent || !card_canvas || !card_canvas_buf) {
        return;
    }
    for (int i = 0; i < inverted_clock_card::kCount; ++i) {
        if (!card_canvas_buf[i]) {
            card_canvas_buf[i] = alloc_canvas_buffer(inverted_clock_card::kWidth,
                                                     inverted_clock_card::kHeight);
        }
        if (!card_canvas_buf[i]) {
            continue;
        }
        card_canvas[i] = lv_canvas_create(parent);
        if (!card_canvas[i]) {
            ESP_LOGW(TAG, INVERTED_CLOCK_CARD_CANVAS_CREATE_FAILED_FORMAT, i);
            continue;
        }
        configure_canvas_base(card_canvas[i],
                              card_canvas_buf[i],
                              inverted_clock_card::kX[i],
                              inverted_clock_card::kY,
                              inverted_clock_card::kWidth,
                              inverted_clock_card::kHeight);
        lv_canvas_fill_bg(card_canvas[i], lv_color_black(), LV_OPA_COVER);
    }
}

bool update_inverted_clock_cards(const struct tm &local,
                                 lv_obj_t *card_canvas[3],
                                 int last_values[3])
{
    if (!card_canvas || !last_values) {
        return false;
    }
    const int values[inverted_clock_card::kCount] = {
        local.tm_hour,
        local.tm_min,
        local.tm_sec,
    };
    bool changed = false;
    for (int i = 0; i < inverted_clock_card::kCount; ++i) {
        changed |= update_inverted_clock_card_value(card_canvas[i],
                                                     values[i],
                                                     &last_values[i]);
    }
    return changed;
}

void clear_inverted_clock_card(lv_obj_t *card_canvas)
{
    inverted_clock_card::clear(card_canvas);
}

bool update_inverted_clock_card_value(lv_obj_t *card_canvas,
                                      int value,
                                      int *last_value)
{
    if (!card_canvas || !last_value || value < 0 || value > 99 || value == *last_value) {
        return false;
    }
    *last_value = value;
    inverted_clock_card::render(card_canvas, value);
    return true;
}
