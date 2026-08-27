#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 第一版 GBC/gnuboy 验证入口。
 *
 * 当前组件先作为 Peanut-GB 之外的独立实验路径存在：
 * - 不接入主菜单；
 * - 不接音频；
 * - 不做 SRAM/即时存档；
 * - 只验证 gnuboy core 能否加载 ROM、跑帧、输出 RGB565 framebuffer。
 */
esp_err_t gbc_emu_start_file(const char *rom_path);
esp_err_t gbc_emu_stop(void);
void gbc_emu_set_joypad(uint8_t joypad);
void gbc_emu_set_volume(uint8_t volume);
uint8_t gbc_emu_get_volume(void);

#ifdef __cplusplus
}
#endif
