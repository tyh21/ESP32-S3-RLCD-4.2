// 声明工作页共用的电池图标构建、样式和电量格刷新接口。
#pragma once

#include "lvgl.h"

void style_battery_part(lv_obj_t *obj, bool filled);
void style_battery_frame(lv_obj_t *obj);
void build_battery_icon(lv_obj_t *parent, lv_obj_t **segments);
void update_battery_segments(lv_obj_t **segments,
                             int percent,
                             bool charging = false,
                             bool blink_on = true);
// 保留旧的多页面刷新接口供兼容调用；UI 主循环使用下方可见页接口。
void update_battery_icon(int percent, bool charging = false, bool blink_on = true);
void update_work_page_battery_icon(int page,
                                   int percent,
                                   bool charging = false,
                                   bool blink_on = true);
