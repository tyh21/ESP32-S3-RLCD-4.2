// 声明温湿历史页曲线、坐标轴和极值徽标的绘制接口。
#pragma once

#include "app_state.h"

inline constexpr int kHistoryAxisValueCount = 3;
inline constexpr int kHistoryAxisTickCount = 5;
inline constexpr int kHistoryChartCanvasX = 18;
inline constexpr int kHistoryChartCanvasY = 82;
inline constexpr int kHistoryBadgeW = 40;
inline constexpr int kHistoryBadgeH = 16;
inline constexpr char kHistoryAxisPlaceholder[] = "--";

void style_history_value_badge(lv_obj_t *label);
void set_history_badge(lv_obj_t *label,
                       const char *text,
                       int canvas_x,
                       int canvas_y,
                       int point_x,
                       int point_y,
                       int plot_x,
                       int plot_y,
                       int plot_w,
                       int plot_h);
void update_history_axis_labels(time_t start, time_t end);
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
                              lv_obj_t **axis_labels);
