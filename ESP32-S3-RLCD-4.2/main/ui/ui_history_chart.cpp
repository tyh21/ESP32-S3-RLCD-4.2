// 绘制温湿历史页曲线、坐标轴和极值徽标。
#include "ui_history_chart.h"

#include "app_constexpr.h"
#include "ui_history_format.h"
#include "ui_history_window.h"
#include "ui_views.h"

namespace {
constexpr int kHistoryAxisMaxIndex = 0;
constexpr int kHistoryAxisMidIndex = 1;
constexpr int kHistoryAxisMinIndex = 2;
constexpr int kHistoryBadgeRadius = 6;
constexpr int kHistoryBadgeHorizontalPad = 3;
constexpr int kHistoryBadgePointGap = 4;
constexpr int kHistoryGridLineCount = 4;
constexpr int kHistoryGridIntervalCount = kHistoryGridLineCount - 1;
constexpr int kHistoryMinValidSamplesForLine = 2;
constexpr int kHistoryMaxConnectedGapHours = 2;
constexpr int kHistoryPointRadius = 3;
constexpr int kHistoryAxisTickHours[] = {0, 6, 12, 18, 24};
constexpr size_t kHistoryAxisHourTextSize = 8;
constexpr size_t kHistoryAxisValueTextSize = 16;

#define HISTORY_CHART_INVALID_ARG_LOG "history chart invalid arg"

static_assert(kHistoryAxisValueCount == kHistoryAxisMinIndex + 1,
              "History axis index count mismatch");
static_assert(kHistoryAxisMaxIndex >= 0 && kHistoryAxisMaxIndex < kHistoryAxisValueCount,
              "History max axis index out of range");
static_assert(kHistoryAxisMidIndex >= 0 && kHistoryAxisMidIndex < kHistoryAxisValueCount,
              "History mid axis index out of range");
static_assert(kHistoryAxisMinIndex >= 0 && kHistoryAxisMinIndex < kHistoryAxisValueCount,
              "History min axis index out of range");
static_assert(kHistoryBadgeW > 0 && kHistoryBadgeH > 0,
              "History badge dimensions must be positive");
static_assert(kHistoryGridLineCount > 1,
              "History grid needs at least two lines");
static_assert(kHistoryGridIntervalCount == kHistoryGridLineCount - 1,
              "History grid interval count mismatch");
static_assert(kHistoryMinValidSamplesForLine >= 2,
              "History line needs at least two samples");
static_assert(array_count(kHistoryAxisTickHours) == kHistoryAxisTickCount,
              "History axis tick hour count mismatch");
static_assert(kHistoryAxisTickHours[0] == 0 &&
                  kHistoryAxisTickHours[kHistoryAxisTickCount - 1] == kHistoryWindowHours,
              "History axis ticks must span the full display window");

float history_sample_value(const HourlySensorSample &sample, bool temperature)
{
    return temperature ? sample.temperature : sample.humidity;
}

void set_history_axis_placeholders(lv_obj_t **axis_labels)
{
    if (!axis_labels) {
        return;
    }
    for (int i = 0; i < kHistoryAxisValueCount; ++i) {
        set_label_text_if_changed(axis_labels[i], kHistoryAxisPlaceholder);
    }
}
} // namespace

void style_history_value_badge(lv_obj_t *label)
{
    if (!label) {
        return;
    }
    lv_obj_set_style_bg_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_border_width(label, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(label, kHistoryBadgeRadius, LV_PART_MAIN);
    lv_obj_set_style_pad_left(label, kHistoryBadgeHorizontalPad, LV_PART_MAIN);
    lv_obj_set_style_pad_right(label, kHistoryBadgeHorizontalPad, LV_PART_MAIN);
    lv_obj_set_style_pad_top(label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label, 0, LV_PART_MAIN);
}

void set_history_badge(lv_obj_t *label,
                       const char *text,
                       int canvas_x,
                       int canvas_y,
                       int point_x,
                       int point_y,
                       int plot_x,
                       int plot_y,
                       int plot_w,
                       int plot_h)
{
    if (!label) {
        return;
    }
    if (plot_w <= 0 || plot_h <= 0) {
        set_obj_visible(label, false);
        return;
    }
    set_label_text_if_changed(label, text ? text : "");
    int x = canvas_x + point_x - kHistoryBadgeW / 2;
    int min_x = canvas_x + plot_x;
    int max_x = canvas_x + plot_x + plot_w - kHistoryBadgeW;
    int min_y = canvas_y + plot_y;
    int max_y = canvas_y + plot_y + plot_h - kHistoryBadgeH;
    int y = canvas_y + point_y - kHistoryBadgeH - kHistoryBadgePointGap;
    if (y < min_y) {
        y = canvas_y + point_y + kHistoryBadgePointGap;
    }
    x = clamp_int(x, min_x, max_x);
    y = clamp_int(y, min_y, max_y);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, kHistoryBadgeW, kHistoryBadgeH);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
}

void update_history_axis_labels(time_t start, time_t end)
{
    (void)end;
    for (int i = 0; i < kHistoryAxisTickCount; ++i) {
        char text[kHistoryAxisHourTextSize] = {};
        format_history_axis_hour(start + kHistoryAxisTickHours[i] * kHistorySecondsPerHour,
                                 text,
                                 sizeof(text));
        set_label_text_if_changed(g_history_time_labels[i], text);
    }
}

void draw_history_chart_panel(lv_obj_t *canvas,
                              int canvas_w,
                              int canvas_h,
                              const HourlySensorSample *samples,
                              int sample_count,
                              bool temperature,
                              int plot_x,
                              int plot_y,
                              int plot_w,
                              int plot_h,
                              lv_obj_t *max_label,
                              lv_obj_t *min_label,
                              lv_obj_t **axis_labels)
{
    if (!canvas || !samples || sample_count <= 0 || !axis_labels ||
        canvas_w <= 0 || canvas_h <= 0 || plot_w <= 0 || plot_h <= 0) {
        ESP_LOGW(TAG, "%s", HISTORY_CHART_INVALID_ARG_LOG);
        set_obj_visible(max_label, false);
        set_obj_visible(min_label, false);
        set_history_axis_placeholders(axis_labels);
        return;
    }
    int valid_count = 0;
    float min_value = 0.0f;
    float max_value = 0.0f;
    int min_index = -1;
    int max_index = -1;
    for (int i = 0; i < sample_count; ++i) {
        if (!samples[i].valid) {
            continue;
        }
        float value = history_sample_value(samples[i], temperature);
        if (valid_count == 0 || value < min_value) {
            min_value = value;
            min_index = i;
        }
        if (valid_count == 0 || value > max_value) {
            max_value = value;
            max_index = i;
        }
        ++valid_count;
    }

    for (int i = 0; i < kHistoryGridLineCount; ++i) {
        int y = plot_y + (plot_h * i) / kHistoryGridIntervalCount;
        canvas_draw_dashed_hline(canvas,
                                 canvas_w,
                                 canvas_h,
                                 plot_x,
                                 plot_x + plot_w,
                                 y,
                                 lv_color_black());
    }
    canvas_draw_line(canvas,
                     canvas_w,
                     canvas_h,
                     plot_x,
                     plot_y + plot_h,
                     plot_x + plot_w,
                     plot_y + plot_h,
                     lv_color_black());

    if (valid_count < kHistoryMinValidSamplesForLine) {
        set_obj_visible(max_label, false);
        set_obj_visible(min_label, false);
        set_history_axis_placeholders(axis_labels);
        return;
    }

    HistoryAxisRange axis = history_axis_range(temperature, min_value, max_value);
    char axis_text[kHistoryAxisValueTextSize] = {};
    format_history_axis_value(temperature, axis.maximum, axis_text, sizeof(axis_text));
    set_label_text_if_changed(axis_labels[kHistoryAxisMaxIndex], axis_text);
    format_history_axis_value(temperature, axis.midpoint, axis_text, sizeof(axis_text));
    set_label_text_if_changed(axis_labels[kHistoryAxisMidIndex], axis_text);
    format_history_axis_value(temperature, axis.minimum, axis_text, sizeof(axis_text));
    set_label_text_if_changed(axis_labels[kHistoryAxisMinIndex], axis_text);

    int prev_x = 0;
    int prev_y = 0;
    time_t prev_ts = 0;
    bool have_prev = false;
    time_t start = samples[0].timestamp - kHistorySecondsPerHour;
    for (int i = 0; i < sample_count; ++i) {
        if (!samples[i].valid) {
            have_prev = false;
            continue;
        }
        int x = plot_x + static_cast<int>(((samples[i].timestamp - start) * plot_w) /
                                           (kHistoryWindowHours * kHistorySecondsPerHour));
        float value = history_sample_value(samples[i], temperature);
        int y = history_value_to_plot_y(value, axis.minimum, axis.maximum, plot_y, plot_h);
        x = clamp_int(x, plot_x, plot_x + plot_w);
        y = clamp_int(y, plot_y, plot_y + plot_h);
        if (have_prev &&
            samples[i].timestamp - prev_ts <=
                kHistoryMaxConnectedGapHours * kHistorySecondsPerHour) {
            canvas_draw_line(canvas,
                             canvas_w,
                             canvas_h,
                             prev_x,
                             prev_y,
                             x,
                             y,
                             lv_color_black());
        }
        prev_x = x;
        prev_y = y;
        prev_ts = samples[i].timestamp;
        have_prev = true;
    }

    if (max_index >= 0) {
        char text[kHistoryAxisValueTextSize] = {};
        float value = history_sample_value(samples[max_index], temperature);
        format_history_badge_value(temperature, value, text, sizeof(text));
        int x = plot_x + static_cast<int>(((samples[max_index].timestamp - start) * plot_w) /
                                           (kHistoryWindowHours * kHistorySecondsPerHour));
        int y = history_value_to_plot_y(value, axis.minimum, axis.maximum, plot_y, plot_h);
        canvas_draw_filled_circle(canvas,
                                  canvas_w,
                                  canvas_h,
                                  x,
                                  y,
                                  kHistoryPointRadius,
                                  lv_color_black());
        set_history_badge(max_label,
                          text,
                          kHistoryChartCanvasX,
                          kHistoryChartCanvasY,
                          x,
                          y,
                          plot_x,
                          plot_y,
                          plot_w,
                          plot_h);
    }
    if (min_index >= 0 && min_index != max_index) {
        char text[kHistoryAxisValueTextSize] = {};
        float value = history_sample_value(samples[min_index], temperature);
        format_history_badge_value(temperature, value, text, sizeof(text));
        int x = plot_x + static_cast<int>(((samples[min_index].timestamp - start) * plot_w) /
                                           (kHistoryWindowHours * kHistorySecondsPerHour));
        int y = history_value_to_plot_y(value, axis.minimum, axis.maximum, plot_y, plot_h);
        canvas_draw_filled_circle(canvas,
                                  canvas_w,
                                  canvas_h,
                                  x,
                                  y,
                                  kHistoryPointRadius,
                                  lv_color_black());
        set_history_badge(min_label,
                          text,
                          kHistoryChartCanvasX,
                          kHistoryChartCanvasY,
                          x,
                          y,
                          plot_x,
                          plot_y,
                          plot_w,
                          plot_h);
    } else {
        set_obj_visible(min_label, false);
    }
}
