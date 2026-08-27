// 声明 DSEG 字形查找、文本绘制和天气时钟数字 canvas 接口。
#pragma once

#include "app_state.h"

const DsegGlyph *find_dseg_glyph(const DsegFont &font, char ch);
int draw_dseg_text(lv_obj_t *canvas, const DsegFont &font, const char *text, int cursor_x, int baseline_y);
void draw_time_canvas(const struct tm &local);
void draw_second_canvas(const struct tm &local);
