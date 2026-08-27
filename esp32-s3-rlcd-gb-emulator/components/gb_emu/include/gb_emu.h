#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char title[17];
    uint8_t cartridge_type;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    uint8_t cgb_flag;
    uint8_t sgb_flag;
    uint8_t destination_code;
    uint8_t mask_rom_version;
    uint8_t header_checksum;
    uint8_t computed_header_checksum;
    size_t expected_rom_size;
    bool header_checksum_ok;
} gb_emu_rom_header_t;

typedef struct {
    uint8_t *data;
    size_t size;
    gb_emu_rom_header_t header;
} gb_emu_rom_t;

esp_err_t gb_emu_load_rom(const char *path, gb_emu_rom_t *rom);
void gb_emu_free_rom(gb_emu_rom_t *rom);
void gb_emu_log_rom_info(const gb_emu_rom_t *rom);

/*
 * 启动一个最小 GB 模拟器任务。
 *
 * 当前阶段只输出画面，不输出声音，也没有完整按键输入。
 * 调用前需要已经初始化 board_rlcd。
 */
esp_err_t gb_emu_start(const gb_emu_rom_t *rom);
esp_err_t gb_emu_stop(void);

/*
 * 设置 GB 按键状态。
 *
 * Peanut-GB 使用低电平有效的 8bit 按键状态：
 *   bit0 A
 *   bit1 B
 *   bit2 Select
 *   bit3 Start
 *   bit4 Right
 *   bit5 Left
 *   bit6 Up
 *   bit7 Down
 *
 * 对应 bit 为 1 表示松开，为 0 表示按下。
 */
void gb_emu_set_joypad(uint8_t joypad);
void gb_emu_set_volume(uint8_t volume);
uint8_t gb_emu_get_volume(void);

#ifdef __cplusplus
}
#endif
