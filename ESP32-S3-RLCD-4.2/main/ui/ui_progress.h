// 声明工作页共享的分段进度条构建和增量刷新接口。
#pragma once

#include "app_state.h"

void draw_progress_segment(lv_obj_t *canvas, int index, bool filled);
void invalidate_progress_segment(lv_obj_t *canvas, int index);
void build_progress_canvas(lv_obj_t *parent, lv_obj_t **canvas, lv_color_t **buf, int y);
void update_progress_canvas(lv_obj_t *canvas, int filled, int *last_filled);
void build_work_page_day_progress(lv_obj_t *parent, int page);
bool update_work_page_day_progress(int page, const struct tm &local);
void set_work_page_day_progress_visible(int page, bool visible);
void clear_work_page_day_progress_refs();
