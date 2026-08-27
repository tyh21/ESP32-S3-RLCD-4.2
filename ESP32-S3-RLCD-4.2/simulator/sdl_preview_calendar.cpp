// 实现 SDL 日历页面主体，集中维护月份布局和预览农历文本。
#include "sdl_preview_calendar.h"

#include <stdio.h>

#include <vector>

#include "core/app_constexpr.h"
#include "sdl_preview_widgets.h"

LV_FONT_DECLARE(zh_font_16);

namespace {
using sdl_preview_widgets::canvas_fill_rect;

constexpr int kCalendarCanvasW = 364;
constexpr int kCalendarCanvasH = 228;
constexpr int kPreviewTmYearOffset = 1900;
constexpr int kPreviewTmMonthOffset = 1;
constexpr int kCalendarColumnCount = 7;

std::vector<lv_color_t> g_calendar_canvas_pixels(kCalendarCanvasW * kCalendarCanvasH);

int preview_days_in_month(int year, int month)
{
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

int preview_first_weekday(int year, int month)
{
    struct tm local = {};
    local.tm_year = year - kPreviewTmYearOffset;
    local.tm_mon = month - kPreviewTmMonthOffset;
    local.tm_mday = 1;
    local.tm_hour = 12;
    mktime(&local);
    return local.tm_wday;
}

void canvas_dot_rect(lv_obj_t *canvas, int x, int y, int w, int h)
{
    for (int yy = y; yy < y + h; yy += 3) {
        for (int xx = x; xx < x + w; xx += 4) {
            lv_canvas_set_px_color(canvas, xx, yy, lv_color_black());
        }
    }
    int right = x + w - 1;
    int bottom = y + h - 1;
    for (int yy = y; yy <= bottom; yy += 3) {
        lv_canvas_set_px_color(canvas, right, yy, lv_color_black());
    }
    for (int xx = x; xx <= right; xx += 4) {
        lv_canvas_set_px_color(canvas, xx, bottom, lv_color_black());
    }
    lv_canvas_set_px_color(canvas, right, bottom, lv_color_black());
}

void canvas_fill_round_rect(lv_obj_t *canvas,
                            int x,
                            int y,
                            int w,
                            int h,
                            int radius,
                            lv_color_t color)
{
    int r2 = radius * radius;
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            int dx = 0;
            if (xx < radius) {
                dx = radius - xx;
            } else if (xx >= w - radius) {
                dx = xx - (w - radius - 1);
            }
            int dy = 0;
            if (yy < radius) {
                dy = radius - yy;
            } else if (yy >= h - radius) {
                dy = yy - (h - radius - 1);
            }
            if (dx == 0 || dy == 0 || dx * dx + dy * dy <= r2) {
                lv_canvas_set_px_color(canvas, x + xx, y + yy, color);
            }
        }
    }
}

void draw_preview_calendar_text(lv_obj_t *canvas,
                                const char *text,
                                int x,
                                int y,
                                int w,
                                const lv_font_t *font,
                                lv_color_t color)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = color;
    dsc.font = font;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_canvas_draw_text(canvas, x, y, w, &dsc, text);
}
} // namespace

void build_calendar_preview_body(lv_obj_t *screen, const struct tm *local)
{
    if (!screen || !local) {
        return;
    }

    lv_obj_t *calendar = lv_canvas_create(screen);
    lv_obj_clear_flag(calendar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(calendar, 18, 63);
    lv_obj_set_size(calendar, kCalendarCanvasW, kCalendarCanvasH);
    lv_obj_set_style_border_width(calendar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(calendar, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(calendar,
                         g_calendar_canvas_pixels.data(),
                         kCalendarCanvasW,
                         kCalendarCanvasH,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(calendar, lv_color_white(), LV_OPA_COVER);

    static constexpr const char *kWeekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    static constexpr const char *kLunarPreview[] = {
        "初一", "初二", "初三", "清明", "初五", "初六", "初七", "初八",
        "初九", "初十", "十一", "十二", "十三", "十四", "十五", "十六",
        "十七", "十八", "十九", "二十", "廿一", "廿二", "廿三", "廿四",
        "廿五", "廿六", "廿七", "廿八", "廿九", "三十", "初一",
    };
    static_assert(array_count(kWeekdays) == kCalendarColumnCount,
                  "calendar preview weekday labels must cover seven columns");
    constexpr int kCellW = 52;
    constexpr int kCellH = 41;
    constexpr int kHeaderH = 24;
    canvas_fill_rect(calendar, 0, 2, kCellW, 18, lv_color_black());
    canvas_fill_rect(calendar, kCellW * 6, 2, kCellW, 18, lv_color_black());
    canvas_dot_rect(calendar, kCellW, 2, kCellW * 5, 18);
    for (int col = 0; col < kCalendarColumnCount; ++col) {
        int x = col * kCellW;
        lv_color_t color = (col == 0 || col == 6) ? lv_color_white() : lv_color_black();
        draw_preview_calendar_text(calendar, kWeekdays[col], x, 2, kCellW, &zh_font_16, color);
    }

    int first = preview_first_weekday(local->tm_year + kPreviewTmYearOffset,
                                      local->tm_mon + kPreviewTmMonthOffset);
    int days = preview_days_in_month(local->tm_year + kPreviewTmYearOffset,
                                     local->tm_mon + kPreviewTmMonthOffset);
    for (int day = 1; day <= days; ++day) {
        int idx = first + day - 1;
        int col = idx % kCalendarColumnCount;
        int row = idx / kCalendarColumnCount;
        int x = col * kCellW;
        int y = kHeaderH + 3 + row * kCellH;
        bool today = day == local->tm_mday;
        if (today) {
            canvas_fill_round_rect(calendar,
                                   x + 4,
                                   y + 3,
                                   kCellW - 8,
                                   kCellH - 5,
                                   5,
                                   lv_color_black());
        }
        char day_text[4];
        snprintf(day_text, sizeof(day_text), "%d", day);
        lv_color_t text_color = today ? lv_color_white() : lv_color_black();
        draw_preview_calendar_text(calendar,
                                   day_text,
                                   x,
                                   y + 2,
                                   kCellW,
                                   &lv_font_montserrat_16,
                                   text_color);
        draw_preview_calendar_text(calendar,
                                   kLunarPreview[(day - 1) % array_count(kLunarPreview)],
                                   x,
                                   y + 20,
                                   kCellW,
                                   &zh_font_16,
                                   text_color);
    }
    lv_obj_invalidate(calendar);
}
