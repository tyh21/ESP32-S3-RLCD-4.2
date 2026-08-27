#pragma once

#include <stdbool.h>
#include "board_rlcd.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 初始化 LVGL9，并把 LVGL 的显示输出接到板载 RLCD。
 *
 * 调用前需要先调用 board_rlcd_init()。
 */
esp_err_t board_lvgl_init(void);

/*
 * 获取 LVGL 默认显示对象。
 *
 * 如果 LVGL 尚未初始化，返回 NULL。
 */
lv_display_t *board_lvgl_get_display(void);

/*
 * 运行时切换 LVGL 逻辑显示方向。
 *
 * 这个函数只改变 LVGL 看到的逻辑分辨率和 board_rlcd 的坐标映射，
 * 不会重新初始化 ST7305。
 */
esp_err_t board_lvgl_set_rotation(board_rlcd_rotation_t rotation);

/*
 * 获取/释放 LVGL 互斥锁。
 *
 * 应用代码创建或修改 LVGL 控件时，建议先 lock，再操作 LVGL，
 * 操作完成后 unlock。
 */
bool board_lvgl_lock(int timeout_ms);
void board_lvgl_unlock(void);

/*
 * 创建一个最小 LVGL9 测试界面。
 */
esp_err_t board_lvgl_create_test_screen(void);

#ifdef __cplusplus
}
#endif
