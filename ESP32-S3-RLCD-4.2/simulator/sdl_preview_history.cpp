// 实现 SDL 温湿历史页面主体，保持设备预览的曲线和标记布局集中维护。
#include "sdl_preview_history.h"

#include <stdio.h>

#include <vector>

#include "sdl_preview_widgets.h"

LV_FONT_DECLARE(zh_font_16);

namespace {
using sdl_preview_widgets::canvas_draw_filled_circle;
using sdl_preview_widgets::canvas_draw_line;
using sdl_preview_widgets::canvas_set_px_safe;
using sdl_preview_widgets::make_label;
using sdl_preview_widgets::make_label_with_font;
using sdl_preview_widgets::set_label_text_if_changed;

constexpr int kHistoryCanvasW = 364;
constexpr int kHistoryCanvasH = 190;
constexpr int kHistoryCanvasX = 18;
constexpr int kHistoryCanvasY = 82;
constexpr int kHistorySampleCount = 24;
constexpr int kHistoryAxisLabelCount = 3;
constexpr int kHistoryTimeLabelCount = 5;
constexpr int kSecondsPerHour = 3600;
constexpr int kHistoryWindowHours = 24;

std::vector<lv_color_t> g_history_chart_canvas_pixels(kHistoryCanvasW * kHistoryCanvasH);

struct PreviewHistorySample {
    float temp;
    float humi;
};

int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

void canvas_draw_dashed_hline(lv_obj_t *canvas,
                              int w,
                              int h,
                              int x1,
                              int x2,
                              int y,
                              lv_color_t color)
{
    for (int x = x1; x <= x2; ++x) {
        if (((x - x1) / 5) % 2 == 0) {
            canvas_set_px_safe(canvas, x, y, w, h, color);
        }
    }
}

int value_to_plot_y(float value, float min_value, float max_value, int y, int h)
{
    float range = max_value - min_value;
    if (range < 0.01f) {
        range = 1.0f;
    }
    float normalized = (value - min_value) / range;
    int offset = static_cast<int>(normalized * (h - 1) + 0.5f);
    return y + h - 1 - offset;
}

void style_history_badge(lv_obj_t *label)
{
    lv_obj_set_style_bg_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_border_width(label, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(label, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(label, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_right(label, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_top(label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label, 0, LV_PART_MAIN);
}

void place_badge(lv_obj_t *label,
                 const char *text,
                 int point_x,
                 int point_y,
                 int plot_x,
                 int plot_y,
                 int plot_w,
                 int plot_h)
{
    set_label_text_if_changed(label, text);
    constexpr int kLabelW = 40;
    constexpr int kLabelH = 16;
    int x = kHistoryCanvasX + point_x - kLabelW / 2;
    int min_y = kHistoryCanvasY + plot_y;
    int y = kHistoryCanvasY + point_y - kLabelH - 4;
    if (y < min_y) {
        y = kHistoryCanvasY + point_y + 4;
    }
    x = clamp_int(x,
                  kHistoryCanvasX + plot_x,
                  kHistoryCanvasX + plot_x + plot_w - kLabelW);
    y = clamp_int(y,
                  min_y,
                  kHistoryCanvasY + plot_y + plot_h - kLabelH);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, kLabelW, kLabelH);
}

void format_axis_hour(time_t value, char *out, size_t out_len)
{
    struct tm local = {};
    localtime_r(&value, &local);
    snprintf(out, out_len, "%02d:00", local.tm_hour);
}

void draw_preview_history_panel(lv_obj_t *canvas,
                                const PreviewHistorySample *samples,
                                bool temperature,
                                int plot_x,
                                int plot_y,
                                int plot_w,
                                int plot_h,
                                lv_obj_t *max_label,
                                lv_obj_t *min_label,
                                lv_obj_t **axis_labels)
{
    float min_value = temperature ? samples[0].temp : samples[0].humi;
    float max_value = min_value;
    int min_index = 0;
    int max_index = 0;
    for (int i = 1; i < kHistorySampleCount; ++i) {
        float value = temperature ? samples[i].temp : samples[i].humi;
        if (value < min_value) {
            min_value = value;
            min_index = i;
        }
        if (value > max_value) {
            max_value = value;
            max_index = i;
        }
    }
    for (int i = 0; i < 4; ++i) {
        int y = plot_y + (plot_h * i) / 3;
        canvas_draw_dashed_hline(canvas,
                                 kHistoryCanvasW,
                                 kHistoryCanvasH,
                                 plot_x,
                                 plot_x + plot_w,
                                 y,
                                 lv_color_black());
    }
    canvas_draw_line(canvas,
                     kHistoryCanvasW,
                     kHistoryCanvasH,
                     plot_x,
                     plot_y + plot_h,
                     plot_x + plot_w,
                     plot_y + plot_h,
                     lv_color_black());

    float pad = temperature ? 0.6f : 3.0f;
    float axis_min = min_value - pad;
    float axis_max = max_value + pad;
    float axis_mid = (axis_min + axis_max) * 0.5f;
    char text[16];
    snprintf(text, sizeof(text), temperature ? "%.0f℃" : "%.0f%%", axis_max);
    set_label_text_if_changed(axis_labels[0], text);
    snprintf(text, sizeof(text), temperature ? "%.0f℃" : "%.0f%%", axis_mid);
    set_label_text_if_changed(axis_labels[1], text);
    snprintf(text, sizeof(text), temperature ? "%.0f℃" : "%.0f%%", axis_min);
    set_label_text_if_changed(axis_labels[2], text);

    int prev_x = 0;
    int prev_y = 0;
    for (int i = 0; i < kHistorySampleCount; ++i) {
        int x = plot_x + ((i + 1) * plot_w) / kHistorySampleCount;
        float value = temperature ? samples[i].temp : samples[i].humi;
        int y = value_to_plot_y(value, axis_min, axis_max, plot_y, plot_h);
        if (i > 0) {
            canvas_draw_line(canvas,
                             kHistoryCanvasW,
                             kHistoryCanvasH,
                             prev_x,
                             prev_y,
                             x,
                             y,
                             lv_color_black());
        }
        prev_x = x;
        prev_y = y;
    }

    snprintf(text,
             sizeof(text),
             temperature ? "%.1f" : "%.0f",
             temperature ? samples[max_index].temp : samples[max_index].humi);
    int max_x = plot_x + ((max_index + 1) * plot_w) / kHistorySampleCount;
    int max_y = value_to_plot_y(max_value, axis_min, axis_max, plot_y, plot_h);
    canvas_draw_filled_circle(canvas,
                              kHistoryCanvasW,
                              kHistoryCanvasH,
                              max_x,
                              max_y,
                              3,
                              lv_color_black());
    place_badge(max_label, text, max_x, max_y, plot_x, plot_y, plot_w, plot_h);

    snprintf(text,
             sizeof(text),
             temperature ? "%.1f" : "%.0f",
             temperature ? samples[min_index].temp : samples[min_index].humi);
    int min_x = plot_x + ((min_index + 1) * plot_w) / kHistorySampleCount;
    int min_y = value_to_plot_y(min_value, axis_min, axis_max, plot_y, plot_h);
    canvas_draw_filled_circle(canvas,
                              kHistoryCanvasW,
                              kHistoryCanvasH,
                              min_x,
                              min_y,
                              3,
                              lv_color_black());
    place_badge(min_label, text, min_x, min_y, plot_x, plot_y, plot_w, plot_h);
}
} // namespace

void build_history_preview_body(lv_obj_t *screen, struct tm *local)
{
    if (!screen || !local) {
        return;
    }

    lv_obj_t *temp_title = make_label(screen, 24, 67, 80, 24, "温度");
    lv_obj_set_style_text_font(temp_title, &zh_font_16, LV_PART_MAIN);
    lv_obj_t *humi_title = make_label(screen, 24, 172, 80, 24, "湿度");
    lv_obj_set_style_text_font(humi_title, &zh_font_16, LV_PART_MAIN);

    lv_obj_t *chart = lv_canvas_create(screen);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(chart, kHistoryCanvasX, kHistoryCanvasY);
    lv_obj_set_size(chart, kHistoryCanvasW, kHistoryCanvasH);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);
    lv_canvas_set_buffer(chart,
                         g_history_chart_canvas_pixels.data(),
                         kHistoryCanvasW,
                         kHistoryCanvasH,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);
    lv_obj_move_foreground(temp_title);
    lv_obj_move_foreground(humi_title);

    constexpr int kTimeX[kHistoryTimeLabelCount] = {42, 110, 178, 246, 314};
    constexpr int kTickHours[kHistoryTimeLabelCount] = {0, 6, 12, 18, 24};
    local->tm_min = 0;
    local->tm_sec = 0;
    time_t end_hour = mktime(local);
    time_t start_hour = end_hour - kHistoryWindowHours * kSecondsPerHour;
    for (int i = 0; i < kHistoryTimeLabelCount; ++i) {
        char text[8];
        format_axis_hour(start_hour + kTickHours[i] * kSecondsPerHour, text, sizeof(text));
        lv_obj_t *label = make_label_with_font(screen,
                                               kTimeX[i] - 20,
                                               274,
                                               48,
                                               18,
                                               text,
                                               &lv_font_montserrat_14);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    lv_obj_t *temp_axis[kHistoryAxisLabelCount] = {};
    lv_obj_t *humi_axis[kHistoryAxisLabelCount] = {};
    for (int i = 0; i < kHistoryAxisLabelCount; ++i) {
        temp_axis[i] = make_label(screen, 332, 84 + i * 30, 56, 18, "--");
        humi_axis[i] = make_label(screen, 332, 186 + i * 30, 56, 18, "--");
        lv_obj_set_style_text_align(temp_axis[i], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_align(humi_axis[i], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }

    lv_obj_t *temp_max = make_label_with_font(screen, 0, 0, 40, 16, "--", &lv_font_montserrat_12);
    lv_obj_t *temp_min = make_label_with_font(screen, 0, 0, 40, 16, "--", &lv_font_montserrat_12);
    lv_obj_t *humi_max = make_label_with_font(screen, 0, 0, 40, 16, "--", &lv_font_montserrat_12);
    lv_obj_t *humi_min = make_label_with_font(screen, 0, 0, 40, 16, "--", &lv_font_montserrat_12);
    style_history_badge(temp_max);
    style_history_badge(temp_min);
    style_history_badge(humi_max);
    style_history_badge(humi_min);

    constexpr PreviewHistorySample kSamples[kHistorySampleCount] = {
        {26.9f, 58}, {26.8f, 57}, {27.2f, 64}, {27.7f, 64}, {26.8f, 59}, {26.3f, 64},
        {26.2f, 64}, {26.3f, 65}, {26.3f, 65}, {26.4f, 66}, {26.4f, 66}, {26.4f, 65},
        {26.2f, 66}, {26.3f, 65}, {26.0f, 66}, {26.1f, 65}, {26.0f, 66}, {25.9f, 68},
        {26.1f, 66}, {26.2f, 65}, {26.4f, 65}, {26.6f, 63}, {26.8f, 63}, {27.0f, 62},
    };
    draw_preview_history_panel(chart, kSamples, true, 34, 10, 276, 62, temp_max, temp_min, temp_axis);
    draw_preview_history_panel(chart, kSamples, false, 34, 112, 276, 62, humi_max, humi_min, humi_axis);
    lv_obj_invalidate(chart);
}
