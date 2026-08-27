// 构建和刷新当月日历页面及其农历节日显示。
#include "ui_views.h"

#include "app_constexpr.h"
#include "app_time_constants.h"
#include "calendar_lunar.h"
#include "sensor_services.h"
#include "ui_battery.h"
#include "ui_calendar_layout.h"

#define CALENDAR_CANVAS_CREATE_FAILED_LOG "calendar canvas create failed"
#define CALENDAR_LAYOUT_INVALID_LOG "calendar layout invalid"

static constexpr int kCalendarCanvasW = 364;
static constexpr int kCalendarCanvasH = 228;
static constexpr int kCalendarCanvasX = 18;
static constexpr int kCalendarCanvasY = 63;
static constexpr int kCalendarTopLineX = 18;
static constexpr int kCalendarTopLineY = 54;
static constexpr int kCalendarTopLineW = 364;
static constexpr int kCalendarTopLineH = 4;
static constexpr int kCalendarSundayColumn = 0;
static constexpr int kCalendarSaturdayColumn = kCalendarWeekdayCount - 1;
static constexpr int kCalendarHeaderY = 2;
static constexpr int kCalendarHeaderH = 18;
static constexpr int kCalendarCellY = 27;
static constexpr int kCalendarCellW = 52;
static constexpr int kCalendarCellH = 41;
static constexpr int kCalendarGridX = 0;
static constexpr int kCalendarDottedFillYStep = 3;
static constexpr int kCalendarDottedFillXStep = 4;
static constexpr int kCalendarTodayInsetX = 4;
static constexpr int kCalendarTodayInsetTop = 3;
static constexpr int kCalendarTodayInsetW = 8;
static constexpr int kCalendarTodayInsetH = 5;
static constexpr int kCalendarTodayRadius = 5;
static constexpr int kCalendarDayTextXInset = 2;
static constexpr int kCalendarDayTextY = 2;
static constexpr int kCalendarDayTextWInset = 4;
static constexpr int kCalendarDayTextH = 14;
static constexpr int kCalendarSubTextY = 20;
static constexpr int kCalendarSubTextH = 12;
static constexpr int kCalendarDayTextSize = 4; // "31" plus terminator, with one byte spare.
static constexpr const char *kCalendarWeekdays[kCalendarWeekdayCount] = {"日", "一", "二", "三", "四", "五", "六"};
static lv_color_t *s_calendar_canvas_buffer;
static int s_last_calendar_drawn_month = -1;
static int s_last_calendar_drawn_day = -1;

static_assert(array_count(kCalendarWeekdays) == kCalendarWeekdayCount,
              "calendar weekday label table must match weekday count");
static_assert(kCalendarCanvasW > 0 && kCalendarCanvasH > 0,
              "calendar canvas size must be positive");
static_assert(kCalendarTopLineW > 0 && kCalendarTopLineH > 0,
              "calendar top separator size must be positive");
static_assert(kCalendarGridX + kCalendarCellW * kCalendarWeekdayCount == kCalendarCanvasW,
              "calendar columns must fill canvas width");
static_assert(kCalendarCellY + (kCalendarVisibleRowCount - 1) * kCalendarCellH +
                      kCalendarSubTextY + kCalendarSubTextH <=
                  kCalendarCanvasH,
              "calendar last visible row text must fit canvas height");
static_assert(kCalendarTodayRadius > 0 &&
                  kCalendarTodayRadius * 2 <= kCalendarCellW - kCalendarTodayInsetW &&
                  kCalendarTodayRadius * 2 <= kCalendarCellH - kCalendarTodayInsetH,
              "calendar today radius must fit highlight box");

static void canvas_fill_rect_safe(lv_obj_t *canvas, int w, int h, int x, int y, int rw, int rh, lv_color_t color)
{
    for (int yy = y; yy < y + rh; ++yy) {
        for (int xx = x; xx < x + rw; ++xx) {
            canvas_set_px_safe(canvas, xx, yy, w, h, color);
        }
    }
}

static void canvas_dot_rect(lv_obj_t *canvas, int w, int h, int x, int y, int rw, int rh)
{
    for (int yy = y; yy < y + rh; yy += kCalendarDottedFillYStep) {
        for (int xx = x; xx < x + rw; xx += kCalendarDottedFillXStep) {
            canvas_set_px_safe(canvas, xx, yy, w, h, lv_color_black());
        }
    }
    int right = x + rw - 1;
    int bottom = y + rh - 1;
    for (int yy = y; yy <= bottom; yy += kCalendarDottedFillYStep) {
        canvas_set_px_safe(canvas, right, yy, w, h, lv_color_black());
    }
    for (int xx = x; xx <= right; xx += kCalendarDottedFillXStep) {
        canvas_set_px_safe(canvas, xx, bottom, w, h, lv_color_black());
    }
    canvas_set_px_safe(canvas, right, bottom, w, h, lv_color_black());
}

static void canvas_fill_round_rect_safe(lv_obj_t *canvas, int w, int h, int x, int y, int rw, int rh, int radius, lv_color_t color)
{
    int r2 = radius * radius;
    for (int yy = 0; yy < rh; ++yy) {
        for (int xx = 0; xx < rw; ++xx) {
            int dx = 0;
            if (xx < radius) {
                dx = radius - xx;
            } else if (xx >= rw - radius) {
                dx = xx - (rw - radius - 1);
            }
            int dy = 0;
            if (yy < radius) {
                dy = radius - yy;
            } else if (yy >= rh - radius) {
                dy = yy - (rh - radius - 1);
            }
            if (dx == 0 || dy == 0 || dx * dx + dy * dy <= r2) {
                canvas_set_px_safe(canvas, x + xx, y + yy, w, h, color);
            }
        }
    }
}

static void draw_calendar_text(lv_obj_t *canvas,
                               const char *text,
                               int x,
                               int y,
                               int w,
                               int h,
                               const lv_font_t *font,
                               lv_color_t color,
                               lv_text_align_t align)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = color;
    dsc.font = font;
    dsc.align = align;
    lv_area_t area = {
        (lv_coord_t)x,
        (lv_coord_t)y,
        (lv_coord_t)(x + w - 1),
        (lv_coord_t)(y + h - 1),
    };
    lv_canvas_draw_text(canvas, x, y, w, &dsc, text);
    lv_obj_invalidate_area(canvas, &area);
}

static void draw_calendar_weekday_header()
{
    canvas_fill_rect_safe(g_calendar_canvas,
                          kCalendarCanvasW,
                          kCalendarCanvasH,
                          kCalendarGridX,
                          kCalendarHeaderY,
                          kCalendarCellW,
                          kCalendarHeaderH,
                          lv_color_black());
    canvas_fill_rect_safe(g_calendar_canvas,
                          kCalendarCanvasW,
                          kCalendarCanvasH,
                          kCalendarGridX + kCalendarCellW * kCalendarSaturdayColumn,
                          kCalendarHeaderY,
                          kCalendarCellW,
                          kCalendarHeaderH,
                          lv_color_black());
    canvas_dot_rect(g_calendar_canvas,
                    kCalendarCanvasW,
                    kCalendarCanvasH,
                    kCalendarGridX + kCalendarCellW,
                    kCalendarHeaderY,
                    kCalendarCellW * (kCalendarWeekdayCount - 2),
                    kCalendarHeaderH);

    for (int col = 0; col < kCalendarWeekdayCount; ++col) {
        int x = kCalendarGridX + col * kCalendarCellW;
        if (col == kCalendarSundayColumn || col == kCalendarSaturdayColumn) {
            draw_calendar_text(g_calendar_canvas,
                               kCalendarWeekdays[col],
                               x,
                               kCalendarHeaderY,
                               kCalendarCellW,
                               kCalendarHeaderH,
                               &zh_font_16,
                               lv_color_white(),
                               LV_TEXT_ALIGN_CENTER);
        } else {
            draw_calendar_text(g_calendar_canvas,
                               kCalendarWeekdays[col],
                               x,
                               kCalendarHeaderY,
                               kCalendarCellW,
                               kCalendarHeaderH,
                               &zh_font_16,
                               lv_color_black(),
                               LV_TEXT_ALIGN_CENTER);
        }
    }
}

static void draw_calendar_day_cell(const struct tm &local,
                                   const CalendarMonthLayout &layout,
                                   int day)
{
    int display_row = -1;
    int col = -1;
    if (!calendar_day_display_position(layout, day, &display_row, &col)) {
        return;
    }
    int x = kCalendarGridX + col * kCalendarCellW;
    int y = kCalendarCellY + display_row * kCalendarCellH;
    bool is_today = day == local.tm_mday;

    if (is_today) {
        canvas_fill_round_rect_safe(g_calendar_canvas,
                                    kCalendarCanvasW,
                                    kCalendarCanvasH,
                                    x + kCalendarTodayInsetX,
                                    y + kCalendarTodayInsetTop,
                                    kCalendarCellW - kCalendarTodayInsetW,
                                    kCalendarCellH - kCalendarTodayInsetH,
                                    kCalendarTodayRadius,
                                    lv_color_black());
    }

    char day_text[kCalendarDayTextSize] = {};
    format_calendar_day_text(day_text, sizeof(day_text), day);
    draw_calendar_text(g_calendar_canvas,
                       day_text,
                       x + kCalendarDayTextXInset,
                       y + kCalendarDayTextY,
                       kCalendarCellW - kCalendarDayTextWInset,
                       kCalendarDayTextH,
                       &lv_font_montserrat_16,
                       is_today ? lv_color_white() : lv_color_black(),
                       LV_TEXT_ALIGN_CENTER);

    struct tm day_tm = local;
    day_tm.tm_mday = day;
    day_tm.tm_hour = 12;
    day_tm.tm_min = 0;
    day_tm.tm_sec = 0;
    mktime(&day_tm);
    CalendarDayInfo info;
    calendar_day_info(day_tm, &info);
    draw_calendar_text(g_calendar_canvas,
                       info.subtext,
                       x + kCalendarDayTextXInset,
                       y + kCalendarSubTextY,
                       kCalendarCellW - kCalendarDayTextWInset,
                       kCalendarSubTextH,
                       &zh_font_16,
                       is_today ? lv_color_white() : lv_color_black(),
                       LV_TEXT_ALIGN_CENTER);
}

static void draw_calendar_grid(const struct tm &local)
{
    if (!g_calendar_canvas) {
        return;
    }
    lv_canvas_fill_bg(g_calendar_canvas, lv_color_white(), LV_OPA_COVER);
    draw_calendar_weekday_header();

    int year = local.tm_year + kTmYearOffset;
    int month = local.tm_mon + kTmMonthOffset;
    int today = local.tm_mday;
    int first_weekday = calendar_first_weekday(year, month);
    int days = calendar_days_in_month(year, month);
    CalendarMonthLayout layout = {};
    if (!calculate_calendar_month_layout(first_weekday, days, today, &layout)) {
        ESP_LOGW(TAG, "%s", CALENDAR_LAYOUT_INVALID_LOG);
        lv_obj_invalidate(g_calendar_canvas);
        return;
    }
    for (int day = 1; day <= days; ++day) {
        draw_calendar_day_cell(local, layout, day);
    }
    lv_obj_invalidate(g_calendar_canvas);
}

bool update_calendar_page(const struct tm &local)
{
    build_calendar_page();
    bool changed = false;
    int month_key = calendar_month_key(local);
    if (month_key != s_last_calendar_drawn_month || local.tm_mday != s_last_calendar_drawn_day) {
        s_last_calendar_drawn_month = month_key;
        s_last_calendar_drawn_day = local.tm_mday;
        draw_calendar_grid(local);
        changed = true;
    }
    changed |= update_work_page_status_time(g_calendar_status_time_label, local);
    changed |= update_work_page_sensor_summary(g_calendar_summary_label);
    return changed;
}

void build_calendar_page()
{
    if (g_calendar_root) {
        return;
    }
    lv_obj_t *screen = create_page_root();
    if (!screen) {
        return;
    }
    g_calendar_root = screen;

    build_battery_icon(screen, g_calendar_battery_segments);
    build_work_page_status_bar(screen,
                               kWorkPageCalendar,
                               &g_calendar_date_label,
                               &g_calendar_summary_label,
                               &g_calendar_status_time_label,
                               true);

    lv_obj_t *top_line = make_bar(screen,
                                  kCalendarTopLineX,
                                  kCalendarTopLineY,
                                  kCalendarTopLineW,
                                  kCalendarTopLineH);
    set_obj_black(top_line, true);
    build_work_page_day_progress(screen, kWorkPageCalendar);

    if (!s_calendar_canvas_buffer) {
        s_calendar_canvas_buffer = alloc_canvas_buffer(kCalendarCanvasW, kCalendarCanvasH);
    }
    if (!s_calendar_canvas_buffer) {
        lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
        update_battery_segments(g_calendar_battery_segments, battery_percent_load());
        return;
    }
    g_calendar_canvas = lv_canvas_create(screen);
    if (!g_calendar_canvas) {
        ESP_LOGW(TAG, "%s", CALENDAR_CANVAS_CREATE_FAILED_LOG);
    } else {
        configure_canvas_base(g_calendar_canvas,
                              s_calendar_canvas_buffer,
                              kCalendarCanvasX,
                              kCalendarCanvasY,
                              kCalendarCanvasW,
                              kCalendarCanvasH);
        lv_canvas_fill_bg(g_calendar_canvas, lv_color_white(), LV_OPA_COVER);
    }

    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    update_battery_segments(g_calendar_battery_segments, battery_percent_load());
}
