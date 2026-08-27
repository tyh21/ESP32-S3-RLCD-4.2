#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 创建音频测试界面，并启动后台任务持续读取 MIC1/MIC2 PCM 数据。
 *
 * 调用前需要完成：
 *   board_rlcd_init()
 *   board_lvgl_init()
 */
esp_err_t audio_wave_ui_start(void);

#ifdef __cplusplus
}
#endif
