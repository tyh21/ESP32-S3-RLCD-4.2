// 读取 packed 1-bit 数据并绘制通用图标和温湿度趋势箭头。
#include "ui_bitmap.h"

#define UI_1BIT_ICON_INVALID_SIZE_FORMAT "1bit icon invalid size %dx%d row=%d"
#define UI_1BIT_ICON_ROW_TOO_SMALL_FORMAT "1bit icon row too small width=%d row=%d min=%d"

namespace {
constexpr int kBitsPerByte = 8;
constexpr uint8_t kPacked1BitMsbMask = 0x80;

int packed_1bit_bytes_per_row(int width)
{
    return (width + kBitsPerByte - 1) / kBitsPerByte;
}

bool icon_draw_args_valid(lv_obj_t *canvas, const uint8_t *bits)
{
    return canvas && bits;
}
}

bool packed_1bit_bit_is_set(const uint8_t *bits, uint32_t bit_index)
{
    if (!bits) {
        return false;
    }
    return bits[bit_index / kBitsPerByte] & (kPacked1BitMsbMask >> (bit_index & (kBitsPerByte - 1)));
}

void draw_1bit_icon(lv_obj_t *canvas,
                    int width,
                    int height,
                    int bytes_per_row,
                    const uint8_t *bits,
                    lv_color_t fg,
                    lv_color_t bg)
{
    if (!icon_draw_args_valid(canvas, bits)) {
        return;
    }
    if (width <= 0 || height <= 0 || bytes_per_row <= 0) {
        ESP_LOGW(TAG, UI_1BIT_ICON_INVALID_SIZE_FORMAT, width, height, bytes_per_row);
        return;
    }
    int min_bytes_per_row = packed_1bit_bytes_per_row(width);
    if (bytes_per_row < min_bytes_per_row) {
        ESP_LOGW(TAG, UI_1BIT_ICON_ROW_TOO_SMALL_FORMAT, width, bytes_per_row, min_bytes_per_row);
        return;
    }
    lv_canvas_fill_bg(canvas, bg, LV_OPA_COVER);
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = bits + y * bytes_per_row;
        for (int x = 0; x < width; ++x) {
            bool set = packed_1bit_bit_is_set(row, static_cast<uint32_t>(x));
            if (set) {
                lv_canvas_set_px_color(canvas, x, y, fg);
            }
        }
    }
    lv_obj_invalidate(canvas);
}

bool update_trend_icon(lv_obj_t *canvas, int trend, int *last_trend)
{
    if (!canvas) {
        return false;
    }
    if (last_trend && *last_trend == trend) {
        return false;
    }
    const uint8_t *bits = nullptr;
    if (trend > 0) {
        bits = trend_up_icon_bits;
    } else if (trend < 0) {
        bits = trend_down_icon_bits;
    }
    if (bits) {
        draw_1bit_icon(canvas,
                       TREND_ICON_WIDTH,
                       TREND_ICON_HEIGHT,
                       TREND_ICON_BYTES_PER_ROW,
                       bits,
                       lv_color_black(),
                       lv_color_white());
    } else {
        lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
        lv_obj_invalidate(canvas);
    }
    if (last_trend) {
        *last_trend = trend;
    }
    return true;
}
