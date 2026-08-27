// 实现 SDL 图片时钟主体，集中维护图库画布和块状数字预览。
#include "sdl_preview_gallery.h"

#include "clock_gallery_images.h"
#include "core/app_constexpr.h"
#include "sdl_preview_widgets.h"

#include <vector>

LV_FONT_DECLARE(zh_font_16);

namespace {
using sdl_preview_widgets::canvas_fill_rect;
using sdl_preview_widgets::draw_1bit_icon;
using sdl_preview_widgets::make_bar;
using sdl_preview_widgets::make_label;
using sdl_preview_widgets::set_obj_black;

constexpr int kImageCanvasX = 20;
constexpr int kImageCanvasY = 66;
constexpr int kDividerX = 252;
constexpr int kDividerY = 70;
constexpr int kDividerW = 3;
constexpr int kTimeCanvasX = 268;
constexpr int kTimeCanvasY = 66;
constexpr int kTimeCanvasW = 112;
constexpr int kTimeCanvasH = 198;
constexpr int kTimeCanvasBufferH = 208;
constexpr int kHourY = 15;
constexpr int kMinuteY = 116;
constexpr int kSayingX = 18;
constexpr int kSayingY = 275;
constexpr int kSayingW = 364;
constexpr int kSayingH = 24;
constexpr int kBlockDigitRows = 7;
constexpr int kBlockDigitColumns = 5;
constexpr int kBlockDigitScale = 10;
constexpr int kBlockDigitGap = 8;
constexpr int kDecimalDigitCount = 10;
constexpr const char *kSayingPreview = "今日无事，适合慢慢来。";

std::vector<lv_color_t> g_gallery_image_canvas_pixels(
    CLOCK_GALLERY_IMAGE_WIDTH * CLOCK_GALLERY_IMAGE_HEIGHT);
std::vector<lv_color_t> g_gallery_time_canvas_pixels(kTimeCanvasW * kTimeCanvasBufferH);

constexpr const char *const kBlockDigits[kDecimalDigitCount][kBlockDigitRows] = {
    {"11111", "10001", "10011", "10101", "11001", "10001", "11111"},
    {"00100", "01100", "00100", "00100", "00100", "00100", "01110"},
    {"11110", "00001", "00001", "11110", "10000", "10000", "11111"},
    {"11110", "00001", "00001", "01110", "00001", "00001", "11110"},
    {"10010", "10010", "10010", "11111", "00010", "00010", "00010"},
    {"11111", "10000", "10000", "11110", "00001", "00001", "11110"},
    {"01111", "10000", "10000", "11110", "10001", "10001", "01110"},
    {"11111", "00001", "00010", "00100", "01000", "01000", "01000"},
    {"01110", "10001", "10001", "01110", "10001", "10001", "01110"},
    {"01110", "10001", "10001", "01111", "00001", "00001", "11110"},
};

static_assert(array_count(kBlockDigits) == kDecimalDigitCount,
              "gallery preview digit table must contain decimal digits");
static_assert(kTimeCanvasBufferH >= kTimeCanvasH,
              "gallery preview time buffer must cover visible canvas");
static_assert(kDividerY + (kImageCanvasY + CLOCK_GALLERY_IMAGE_HEIGHT - kDividerY) ==
                  kImageCanvasY + CLOCK_GALLERY_IMAGE_HEIGHT,
              "gallery preview divider must end at image bottom");

void draw_block_digit(lv_obj_t *canvas, int digit, int x, int y)
{
    if (digit < 0 || digit >= kDecimalDigitCount) {
        return;
    }
    for (int row = 0; row < kBlockDigitRows; ++row) {
        for (int col = 0; col < kBlockDigitColumns; ++col) {
            if (kBlockDigits[digit][row][col] == '1') {
                canvas_fill_rect(canvas,
                                 x + col * kBlockDigitScale,
                                 y + row * kBlockDigitScale,
                                 kBlockDigitScale - 1,
                                 kBlockDigitScale - 1,
                                 lv_color_black());
            }
        }
    }
}

void draw_block_number(lv_obj_t *canvas, int value, int y)
{
    constexpr int kDigitW = kBlockDigitColumns * kBlockDigitScale;
    constexpr int kTotalW = kDigitW * 2 + kBlockDigitGap;
    int x = (kTimeCanvasW - kTotalW) / 2;
    draw_block_digit(canvas, value / 10, x, y);
    draw_block_digit(canvas, value % 10, x + kDigitW + kBlockDigitGap, y);
}
} // namespace

void build_gallery_preview_body(lv_obj_t *screen, const struct tm *local)
{
    if (!screen || !local) {
        return;
    }

    lv_obj_t *image_canvas = lv_canvas_create(screen);
    lv_obj_clear_flag(image_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(image_canvas, kImageCanvasX, kImageCanvasY);
    lv_obj_set_size(image_canvas, CLOCK_GALLERY_IMAGE_WIDTH, CLOCK_GALLERY_IMAGE_HEIGHT);
    lv_obj_set_style_border_width(image_canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(image_canvas, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(image_canvas,
                         g_gallery_image_canvas_pixels.data(),
                         CLOCK_GALLERY_IMAGE_WIDTH,
                         CLOCK_GALLERY_IMAGE_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    draw_1bit_icon(image_canvas,
                   CLOCK_GALLERY_IMAGE_WIDTH,
                   CLOCK_GALLERY_IMAGE_HEIGHT,
                   CLOCK_GALLERY_IMAGE_BYTES_PER_ROW,
                   clock_gallery_images[local->tm_wday % CLOCK_GALLERY_IMAGE_COUNT],
                   lv_color_black(),
                   lv_color_white());

    lv_obj_t *divider = make_bar(screen,
                                 kDividerX,
                                 kDividerY,
                                 kDividerW,
                                 kImageCanvasY + CLOCK_GALLERY_IMAGE_HEIGHT - kDividerY);
    set_obj_black(divider, true);

    lv_obj_t *time_canvas = lv_canvas_create(screen);
    lv_obj_clear_flag(time_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(time_canvas, kTimeCanvasX, kTimeCanvasY);
    lv_obj_set_size(time_canvas, kTimeCanvasW, kTimeCanvasH);
    lv_obj_set_style_border_width(time_canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(time_canvas, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(time_canvas,
                         g_gallery_time_canvas_pixels.data(),
                         kTimeCanvasW,
                         kTimeCanvasH,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(time_canvas, lv_color_white(), LV_OPA_COVER);
    draw_block_number(time_canvas, local->tm_hour, kHourY);
    draw_block_number(time_canvas, local->tm_min, kMinuteY);
    lv_obj_invalidate(time_canvas);

    lv_obj_t *saying = make_label(screen,
                                  kSayingX,
                                  kSayingY,
                                  kSayingW,
                                  kSayingH,
                                  kSayingPreview);
    lv_obj_set_style_text_font(saying, &zh_font_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(saying, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(saying, LV_LABEL_LONG_DOT);
}
