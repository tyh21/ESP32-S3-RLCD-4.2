// 声明页面 canvas 复用的基础线段与实心圆绘制图元。
#pragma once

#include "lvgl.h"

void canvas_draw_line(lv_obj_t *canvas,
                      int width,
                      int height,
                      int x0,
                      int y0,
                      int x1,
                      int y1,
                      lv_color_t color);
void canvas_draw_filled_circle(lv_obj_t *canvas,
                               int width,
                               int height,
                               int cx,
                               int cy,
                               int radius,
                               lv_color_t color);
