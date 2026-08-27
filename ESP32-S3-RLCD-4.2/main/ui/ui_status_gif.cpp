// 读取内置或自定义状态 GIF 帧，并执行差分局部绘制。
#include "ui_status_gif.h"

#include <cstring>

#include "custom_assets.h"
#include "ui_bitmap.h"
#include "ui_draw_cache.h"
#include "ui_views.h"

namespace {
static_assert(STATUS_GIF_FRAME_COUNT > 0, "status gif must contain at least one frame");
int s_last_status_gif_frame = -1;

bool is_status_gif_frame_index(int frame)
{
    return frame >= 0 && frame < STATUS_GIF_FRAME_COUNT;
}

int clamp_status_gif_frame(int frame)
{
    if (frame < 0) {
        return 0;
    }
    if (frame >= STATUS_GIF_FRAME_COUNT) {
        return STATUS_GIF_FRAME_COUNT - 1;
    }
    return frame;
}

const uint8_t *builtin_status_gif_frame_or_null(int frame)
{
    return is_status_gif_frame_index(frame) ? status_gif_frames[frame] : nullptr;
}

void expand_area_to_include_pixel(int x, int y, int *min_x, int *min_y, int *max_x, int *max_y)
{
    if (!min_x || !min_y || !max_x || !max_y) {
        return;
    }
    if (x < *min_x) {
        *min_x = x;
    }
    if (x > *max_x) {
        *max_x = x;
    }
    if (y < *min_y) {
        *min_y = y;
    }
    if (y > *max_y) {
        *max_y = y;
    }
}

bool change_area_valid(bool changed, int min_x, int min_y, int max_x, int max_y)
{
    return changed && min_x <= max_x && min_y <= max_y;
}

}

void invalidate_status_gif_draw_cache()
{
    s_last_status_gif_frame = -1;
}

void draw_status_gif_frame(int frame)
{
    if (!g_status_gif_canvas) {
        return;
    }
    frame = clamp_status_gif_frame(frame);
    static uint8_t custom_frame[STATUS_GIF_BYTES_PER_FRAME];
    static uint8_t custom_prev_frame[STATUS_GIF_BYTES_PER_FRAME];
    static bool custom_prev_valid = false;
    const uint8_t *pixels = builtin_status_gif_frame_or_null(frame);
    const uint8_t *prev_pixels = builtin_status_gif_frame_or_null(s_last_status_gif_frame);
    bool using_custom = false;
    if (custom_assets_read_main_gif_frame(frame, custom_frame, sizeof(custom_frame))) {
        pixels = custom_frame;
        using_custom = true;
        prev_pixels = custom_prev_valid ? custom_prev_frame : nullptr;
    } else {
        if (custom_prev_valid) {
            prev_pixels = nullptr;
        }
        custom_prev_valid = false;
    }
    uint32_t bit = 0;
    bool changed = false;
    int min_x = STATUS_GIF_WIDTH;
    int min_y = STATUS_GIF_HEIGHT;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < STATUS_GIF_HEIGHT; ++y) {
        for (int x = 0; x < STATUS_GIF_WIDTH; ++x, ++bit) {
            bool black = packed_1bit_bit_is_set(pixels, bit);
            if (prev_pixels) {
                bool prev_black = packed_1bit_bit_is_set(prev_pixels, bit);
                if (black == prev_black) {
                    continue;
                }
            }
            lv_canvas_set_px_color(g_status_gif_canvas, x, y, black ? lv_color_black() : lv_color_white());
            changed = true;
            expand_area_to_include_pixel(x, y, &min_x, &min_y, &max_x, &max_y);
        }
    }
    if (changed || s_last_status_gif_frame != frame) {
        if (change_area_valid(changed, min_x, min_y, max_x, max_y)) {
            invalidate_canvas_rect(g_status_gif_canvas, min_x, min_y, max_x, max_y);
        } else {
            lv_obj_invalidate(g_status_gif_canvas);
        }
    }
    s_last_status_gif_frame = frame;
    if (using_custom) {
        memcpy(custom_prev_frame, custom_frame, sizeof(custom_prev_frame));
        custom_prev_valid = true;
    }
}
