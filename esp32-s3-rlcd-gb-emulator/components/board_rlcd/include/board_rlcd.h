#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 本开发板屏幕分辨率。
 *
 * 官方示例使用横屏 400x300：
 *   DisplayPort RlcdPort(12, 11, 5, 40, 41, 400, 300);
 */
#define BOARD_RLCD_WIDTH   400
#define BOARD_RLCD_HEIGHT  300
#define BOARD_RLCD_MAX_LOGICAL_WIDTH   BOARD_RLCD_WIDTH
#define BOARD_RLCD_MAX_LOGICAL_HEIGHT  BOARD_RLCD_HEIGHT

/*
 * 反射式 LCD 只有黑/白两种像素状态。
 *
 * 官方示例里：
 *   ColorBlack = 0
 *   ColorWhite = 0xff
 */
typedef enum {
    BOARD_RLCD_COLOR_BLACK = 0,
    BOARD_RLCD_COLOR_WHITE = 0xff,
} board_rlcd_color_t;

typedef enum {
    BOARD_RLCD_ROTATION_0 = 0,
    BOARD_RLCD_ROTATION_90,
    BOARD_RLCD_ROTATION_180,
    BOARD_RLCD_ROTATION_270,
} board_rlcd_rotation_t;

/*
 * 初始化板载 RLCD 屏幕。
 *
 * 这个函数会完成：
 * 1. 初始化 SPI 总线；
 * 2. 创建 ESP-IDF LCD SPI IO 句柄；
 * 3. 配置屏幕复位 GPIO；
 * 4. 按官方示例发送屏幕初始化命令；
 * 5. 清空本地显存为白色。
 */
esp_err_t board_rlcd_init(void);

/*
 * 查询屏幕是否已经初始化。
 */
bool board_rlcd_is_initialized(void);

/*
 * 设置/获取屏幕逻辑方向。
 *
 * 这个接口不会重新初始化 ST7305，也不会修改 0x36。
 * 它只改变逻辑坐标到物理坐标的转换规则。
 */
esp_err_t board_rlcd_set_rotation(board_rlcd_rotation_t rotation);
board_rlcd_rotation_t board_rlcd_get_rotation(void);

/*
 * 获取当前逻辑宽高。
 *
 * 0/180 度：400 x 300
 * 90/270 度：300 x 400
 */
uint16_t board_rlcd_get_logical_width(void);
uint16_t board_rlcd_get_logical_height(void);

/*
 * 清空本地显存。
 *
 * 注意：这个函数只修改 ESP32-S3 内存里的屏幕缓冲区。
 * 如果想让屏幕真的更新，需要再调用 board_rlcd_flush()。
 */
esp_err_t board_rlcd_clear(board_rlcd_color_t color);

/*
 * 设置本地显存中的一个逻辑像素点。
 *
 * 注意：这个函数会先把逻辑坐标转换成物理屏幕坐标，
 * 然后修改 framebuffer，不会立即 flush 到屏幕。
 */
esp_err_t board_rlcd_set_pixel(uint16_t x, uint16_t y, board_rlcd_color_t color);

/*
 * 快速绘制一行 2 倍放大的 Game Boy 灰度像素。
 *
 * pixels 指向 160 个 GB 像素，每个像素只使用低 2 bit：
 *   0/1 -> 白
 *   2/3 -> 黑
 *
 * 这个接口是给 GB 模拟器使用的快速路径，会直接修改 framebuffer。
 * 当前快速路径只针对 0 度横屏优化；其他旋转方向会退回通用 set_pixel。
 */
esp_err_t board_rlcd_draw_gb_line_2x(uint16_t dst_x, uint16_t dst_y, const uint8_t *pixels, uint16_t pixel_count);

/*
 * 快速绘制一行 2 倍放大的 Game Boy Color RGB565 像素。
 *
 * pixels 指向 160 个 RGB565 big-endian 像素。这个接口会把彩色像素
 * 转成 4 级灰度，再用 2x2 黑白抖动写入 RLCD framebuffer。
 *
 * 当前快速路径只针对 0 度横屏优化；其他旋转方向会退回通用 set_pixel。
 */
esp_err_t board_rlcd_draw_gbc_line_2x_rgb565_be(uint16_t dst_x,
                                                uint16_t dst_y,
                                                const uint16_t *pixels,
                                                uint16_t pixel_count);

/*
 * 把本地显存一次性推送到屏幕。
 */
esp_err_t board_rlcd_flush(void);

/*
 * 兼容旧命名。
 *
 * refresh 在这里容易被误解为“清屏/重置后刷新”，新代码优先使用 flush。
 */
esp_err_t board_rlcd_refresh(void);

/*
 * 把本地显存中的一个区域推送到屏幕。
 *
 * 注意：RLCD 控制器的显存不是普通的逐行排列。
 * 这个函数会自动把用户传入的像素区域扩展到屏幕控制器能接受的物理块边界：
 *   - 横向按 2 像素对齐；
 *   - 纵向按 12 像素对齐。
 */
esp_err_t board_rlcd_flush_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/*
 * 兼容旧命名。新代码优先使用 board_rlcd_flush_area()。
 */
esp_err_t board_rlcd_refresh_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/*
 * 把 RGB565 逻辑区域转换成 RLCD 的黑白 framebuffer，并 flush 到屏幕。
 *
 * 这个接口主要给 LVGL flush 回调用。
 */
esp_err_t board_rlcd_flush_rgb565_area(uint16_t x1,
                                       uint16_t y1,
                                       uint16_t x2,
                                       uint16_t y2,
                                       const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
