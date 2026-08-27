#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 画一个最小测试图案并刷新到屏幕。
 *
 * 这个组件只用于调试屏幕，不属于 RLCD 底层驱动。
 */
esp_err_t rlcd_test_pattern_draw_basic(void);

/*
 * 画一个专门用于检查屏幕方向的测试图案并刷新到屏幕。
 */
esp_err_t rlcd_test_pattern_draw_orientation(void);

/*
 * 测试 RLCD 的整屏 flush 速度。
 */
esp_err_t rlcd_test_pattern_benchmark_flush(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
