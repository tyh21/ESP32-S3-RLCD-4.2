// 构建和刷新温湿历史页面及工作页顶部温湿度摘要。
#include "ui_views.h"

#include "app_constexpr.h"
#include "sensor_services.h"
#include "ui_battery.h"
#include "ui_draw_cache.h"
#include "ui_history_chart.h"
#include "ui_history_window.h"

namespace {
constexpr int kHoursPerDay = 24;
constexpr int kHistoryCanvasW = 364;
constexpr int kHistoryCanvasH = 190;
constexpr int kHistoryTopLineX = 18;
constexpr int kHistoryTopLineY = 54;
constexpr int kHistoryTopLineW = 364;
constexpr int kHistoryTopLineH = 4;
constexpr int kHistoryTitleX = 24;
constexpr int kHistoryTempTitleY = 67;
constexpr int kHistoryHumiTitleY = 172;
constexpr int kHistoryTitleW = 80;
constexpr int kHistoryTitleH = 24;
constexpr int kHistoryPlotX = 34;
constexpr int kHistoryTempPlotY = 10;
constexpr int kHistoryHumiPlotY = 112;
constexpr int kHistoryPlotW = 276;
constexpr int kHistoryPlotH = 62;
constexpr int kHistoryTimeLabelW = 48;
constexpr int kHistoryTimeLabelH = 18;
constexpr int kHistoryTimeLabelY = 274;
constexpr int kHistoryTimeLabelCenterX[] = {42, 110, 178, 246, 314};
constexpr int kHistoryAxisLabelX = 332;
constexpr int kHistoryAxisLabelW = 56;
constexpr int kHistoryAxisLabelH = 18;
constexpr int kHistoryTempAxisLabelY = 84;
constexpr int kHistoryHumiAxisLabelY = 186;
constexpr int kHistoryAxisLabelRowGap = 30;
constexpr const char *kHistoryTimePlaceholder = "--:--";
constexpr const char *kHistoryTempTitle = "温度";
constexpr const char *kHistoryHumiTitle = "湿度";
lv_color_t *s_history_chart_canvas_buffer;
uint32_t s_last_history_drawn_version = static_cast<uint32_t>(-1);
int s_last_history_drawn_hour = -1;
static_assert(kHoursPerDay > 0, "Hours per day must be positive");
static_assert(kHistoryTopLineW > 0 && kHistoryTopLineH > 0, "History top separator size must be positive");
static_assert(kHistoryTitleW > 0 && kHistoryTitleH > 0, "History title size must be positive");
static_assert(kHistoryTitleX >= 0 && kHistoryTitleX + kHistoryTitleW <= kHistoryCanvasW,
              "History titles must fit canvas width");
static_assert(kHistoryAxisTickCount > 0, "History axis tick count must be positive");
static_assert(kHistoryCanvasW > 0 && kHistoryCanvasH > 0, "History canvas dimensions must be positive");
static_assert(kHistoryBadgeW > 0 && kHistoryBadgeH > 0, "History badge dimensions must be positive");
static_assert(kHistoryPlotW > 0 && kHistoryPlotH > 0, "History plot dimensions must be positive");
static_assert(kHistoryPlotX >= 0 && kHistoryPlotX + kHistoryPlotW <= kHistoryCanvasW,
              "History plot width must fit canvas");
static_assert(kHistoryTempPlotY >= 0 && kHistoryTempPlotY + kHistoryPlotH <= kHistoryCanvasH,
              "History temperature plot must fit canvas");
static_assert(kHistoryHumiPlotY >= 0 && kHistoryHumiPlotY + kHistoryPlotH <= kHistoryCanvasH,
              "History humidity plot must fit canvas");
static_assert(array_count(kHistoryTimeLabelCenterX) == kHistoryAxisTickCount,
              "History time label coordinate count mismatch");
static_assert(array_count(g_history_time_labels) == kHistoryAxisTickCount,
              "History time label storage must match axis ticks");
static_assert(array_count(g_history_temp_axis_labels) == kHistoryAxisValueCount &&
                  array_count(g_history_humi_axis_labels) == kHistoryAxisValueCount,
              "History value label storage must match axis values");
#define HISTORY_WINDOW_INVALID_ARG_LOG "history window invalid arg"
#define HISTORY_TEMP_TITLE_CREATE_FAILED_LOG "history temp title create failed"
#define HISTORY_HUMI_TITLE_CREATE_FAILED_LOG "history humi title create failed"
#define HISTORY_CHART_CANVAS_CREATE_FAILED_LOG "history chart canvas create failed"
#define HISTORY_TIME_LABEL_CREATE_FAILED_FORMAT "history time label create failed index=%d"
#define HISTORY_TEMP_AXIS_LABEL_CREATE_FAILED_FORMAT "history temp axis label create failed index=%d"
#define HISTORY_HUMI_AXIS_LABEL_CREATE_FAILED_FORMAT "history humi axis label create failed index=%d"

bool history_window_args_valid(const HourlySensorSample *out, const int *out_count)
{
    return out && out_count;
}
} // namespace

void invalidate_history_draw_cache()
{
    s_last_history_drawn_version = static_cast<uint32_t>(-1);
    s_last_history_drawn_hour = -1;
}

enum class HistoryLabelLogKind {
    kTime,
    kTempAxis,
    kHumiAxis,
};

static lv_obj_t *make_history_title(lv_obj_t *screen,
                                    int y,
                                    const char *text,
                                    const char *failure_log)
{
    lv_obj_t *label = make_label(screen,
                                 kHistoryTitleX,
                                 y,
                                 kHistoryTitleW,
                                 kHistoryTitleH,
                                 text);
    if (label) {
        lv_obj_set_style_text_font(label, &zh_font_16, LV_PART_MAIN);
    } else {
        ESP_LOGW(TAG, "%s", failure_log);
    }
    return label;
}

static void build_history_value_badges(lv_obj_t *screen)
{
    lv_obj_t **badges[] = {
        &g_history_temp_max_label,
        &g_history_temp_min_label,
        &g_history_humi_max_label,
        &g_history_humi_min_label,
    };
    for (lv_obj_t **badge : badges) {
        *badge = make_label_with_font(screen,
                                      0,
                                      0,
                                      kHistoryBadgeW,
                                      kHistoryBadgeH,
                                      kHistoryAxisPlaceholder,
                                      &lv_font_montserrat_12);
    }
    for (lv_obj_t **badge : badges) {
        style_history_value_badge(*badge);
    }
}

bool collect_history_window(time_t end_hour,
                            HourlySensorSample *out,
                            int *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!history_window_args_valid(out, out_count)) {
        ESP_LOGW(TAG, "%s", HISTORY_WINDOW_INVALID_ARG_LOG);
        return false;
    }
    HourlySensorHistoryBlob history = {};
    if (!get_hourly_sensor_history_snapshot(&history, nullptr)) {
        return false;
    }
    return collect_history_window_from_snapshot(end_hour, history, out, out_count);
}

int history_hour_key(const struct tm &local)
{
    return local.tm_yday * kHoursPerDay + local.tm_hour;
}

void align_history_label_or_log(lv_obj_t *label,
                                lv_text_align_t align,
                                HistoryLabelLogKind log_kind,
                                int index)
{
    if (label) {
        lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    } else {
        switch (log_kind) {
        case HistoryLabelLogKind::kTime:
            ESP_LOGW(TAG, HISTORY_TIME_LABEL_CREATE_FAILED_FORMAT, index);
            break;
        case HistoryLabelLogKind::kTempAxis:
            ESP_LOGW(TAG, HISTORY_TEMP_AXIS_LABEL_CREATE_FAILED_FORMAT, index);
            break;
        case HistoryLabelLogKind::kHumiAxis:
            ESP_LOGW(TAG, HISTORY_HUMI_AXIS_LABEL_CREATE_FAILED_FORMAT, index);
            break;
        }
    }
}

bool update_history_page(const struct tm &local)
{
    build_history_page();
    if (!g_history_chart_canvas) {
        return false;
    }
    struct tm mutable_local = local;
    time_t now = mktime(&mutable_local);
    time_t end_hour = hour_start_from_time(now);
    int hour_key = history_hour_key(local);
    bool changed = update_work_page_status_time(g_history_status_time_label, local);
    changed |= update_work_page_sensor_summary(g_history_summary_label);
    HourlySensorHistoryBlob history = {};
    uint32_t history_version = 0;
    if (!get_hourly_sensor_history_snapshot(&history, &history_version)) {
        return changed;
    }
    if (s_last_history_drawn_version == history_version &&
        s_last_history_drawn_hour == hour_key) {
        return changed;
    }
    s_last_history_drawn_version = history_version;
    s_last_history_drawn_hour = hour_key;

    lv_canvas_fill_bg(g_history_chart_canvas, lv_color_white(), LV_OPA_COVER);

    HourlySensorSample samples[kHistoryWindowHours] = {};
    int sample_count = 0;
    collect_history_window_from_snapshot(end_hour, history, samples, &sample_count);
    time_t start = end_hour - kHistoryWindowHours * kHistorySecondsPerHour;
    update_history_axis_labels(start, end_hour);

    draw_history_chart_panel(g_history_chart_canvas,
                             kHistoryCanvasW,
                             kHistoryCanvasH,
                             samples,
                             sample_count,
                             true,
                             kHistoryPlotX,
                             kHistoryTempPlotY,
                             kHistoryPlotW,
                             kHistoryPlotH,
                             g_history_temp_max_label,
                             g_history_temp_min_label,
                             g_history_temp_axis_labels);
    draw_history_chart_panel(g_history_chart_canvas,
                             kHistoryCanvasW,
                             kHistoryCanvasH,
                             samples,
                             sample_count,
                             false,
                             kHistoryPlotX,
                             kHistoryHumiPlotY,
                             kHistoryPlotW,
                             kHistoryPlotH,
                             g_history_humi_max_label,
                             g_history_humi_min_label,
                             g_history_humi_axis_labels);
    lv_obj_invalidate(g_history_chart_canvas);
    return true;
}

static void build_history_chart_area(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }
    lv_obj_t *history_top_line = make_bar(screen,
                                          kHistoryTopLineX,
                                          kHistoryTopLineY,
                                          kHistoryTopLineW,
                                          kHistoryTopLineH);
    set_obj_black(history_top_line, true);
    lv_obj_t *temp_title = make_history_title(screen,
                                              kHistoryTempTitleY,
                                              kHistoryTempTitle,
                                              HISTORY_TEMP_TITLE_CREATE_FAILED_LOG);
    lv_obj_t *humi_title = make_history_title(screen,
                                              kHistoryHumiTitleY,
                                              kHistoryHumiTitle,
                                              HISTORY_HUMI_TITLE_CREATE_FAILED_LOG);

    if (!s_history_chart_canvas_buffer) {
        s_history_chart_canvas_buffer = alloc_canvas_buffer(kHistoryCanvasW, kHistoryCanvasH);
    }
    if (s_history_chart_canvas_buffer) {
        g_history_chart_canvas = lv_canvas_create(screen);
        if (!g_history_chart_canvas) {
            ESP_LOGW(TAG, "%s", HISTORY_CHART_CANVAS_CREATE_FAILED_LOG);
        } else {
            configure_canvas_base(g_history_chart_canvas,
                                  s_history_chart_canvas_buffer,
                                  kHistoryChartCanvasX,
                                  kHistoryChartCanvasY,
                                  kHistoryCanvasW,
                                  kHistoryCanvasH);
            lv_canvas_fill_bg(g_history_chart_canvas, lv_color_white(), LV_OPA_COVER);
        }
    }
    if (temp_title) {
        lv_obj_move_foreground(temp_title);
    }
    if (humi_title) {
        lv_obj_move_foreground(humi_title);
    }
}

static void build_history_axis_labels(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }
    for (int i = 0; i < kHistoryAxisTickCount; ++i) {
        g_history_time_labels[i] = make_label_with_font(screen,
                                                        kHistoryTimeLabelCenterX[i] - kHistoryTimeLabelW / 2,
                                                        kHistoryTimeLabelY,
                                                        kHistoryTimeLabelW,
                                                        kHistoryTimeLabelH,
                                                        kHistoryTimePlaceholder,
                                                        &lv_font_montserrat_14);
        align_history_label_or_log(g_history_time_labels[i],
                                   LV_TEXT_ALIGN_CENTER,
                                   HistoryLabelLogKind::kTime,
                                   i);
    }
    for (int i = 0; i < kHistoryAxisValueCount; ++i) {
        g_history_temp_axis_labels[i] = make_label(screen,
                                                   kHistoryAxisLabelX,
                                                   kHistoryTempAxisLabelY + i * kHistoryAxisLabelRowGap,
                                                   kHistoryAxisLabelW,
                                                   kHistoryAxisLabelH,
                                                   kHistoryAxisPlaceholder);
        g_history_humi_axis_labels[i] = make_label(screen,
                                                   kHistoryAxisLabelX,
                                                   kHistoryHumiAxisLabelY + i * kHistoryAxisLabelRowGap,
                                                   kHistoryAxisLabelW,
                                                   kHistoryAxisLabelH,
                                                   kHistoryAxisPlaceholder);
        align_history_label_or_log(g_history_temp_axis_labels[i],
                                   LV_TEXT_ALIGN_LEFT,
                                   HistoryLabelLogKind::kTempAxis,
                                   i);
        align_history_label_or_log(g_history_humi_axis_labels[i],
                                   LV_TEXT_ALIGN_LEFT,
                                   HistoryLabelLogKind::kHumiAxis,
                                   i);
    }
}

void build_history_page()
{
    if (g_history_root) {
        return;
    }
    lv_obj_t *screen = create_page_root();
    if (!screen) {
        return;
    }
    g_history_root = screen;

    build_battery_icon(screen, g_history_battery_segments);
    build_work_page_status_bar(screen,
                               kWorkPageHistory,
                               &g_history_date_label,
                               &g_history_summary_label,
                               &g_history_status_time_label,
                               true);

    build_history_chart_area(screen);
    build_work_page_day_progress(screen, kWorkPageHistory);
    build_history_axis_labels(screen);
    build_history_value_badges(screen);

    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    update_battery_segments(g_history_battery_segments, battery_percent_load());
}
