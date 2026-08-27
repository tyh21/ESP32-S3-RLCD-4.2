#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 蓝牙手柄（BLE HID）。
 *
 * 启动后可扫描 BLE HID 设备，用户在屏幕上选择设备连接，
 * 然后进入按键学习模式：屏幕依次提示 10 个手柄键（含 X/Y），
 * 用户在手柄上按下对应键，系统记录字段级 HID → GB 按键映射。
 * 映射保存到 NVS，下次开机自动加载。
 *
 * 按键状态格式与 web_gamepad 相同（8bit 低有效）：
 *   bit0 A, bit1 B, bit2 Select, bit3 Start,
 *   bit4 Right, bit5 Left, bit6 Up, bit7 Down
 * （1 = 松开, 0 = 按下）。Game Boy 只有两个面键，因此 BLE 手柄的
 * X 映射为 A，Y 映射为 B；网页手柄协议保持现有八位格式不变。
 *
 * 与 WiFi 网页手柄并存，两者按键状态取逻辑与（任一按下即生效）。
 */

#define BT_GAMEPAD_MAX_SCAN_RESULTS 8
#define BT_GAMEPAD_MAX_RAW_LEN     16
#define BT_GAMEPAD_NUM_KEYS         10

/* 扫描到的设备信息 */
typedef struct {
    uint8_t  bda[6];        /* BLE 地址 */
    uint8_t  addr_type;     /* 地址类型 */
    char     name[32];      /* 设备名称 */
    uint16_t appearance;    /* 外观 */
    bool     used;          /* 该槽位是否有效 */
} bt_gamepad_scan_result_t;

/* GB 按键名称（用于屏幕显示） */
extern const char *bt_gamepad_key_names[BT_GAMEPAD_NUM_KEYS];

/*
 * 初始化蓝牙控制器和协议栈，启动后台扫描。
 * 扫描到的设备会填充到内部列表中，可用 bt_gamepad_get_scan_results() 读取。
 */
esp_err_t bt_gamepad_start(void);

/*
 * 开始/重新开始扫描 BLE 设备（持续 5 秒）。
 */
esp_err_t bt_gamepad_start_scan(void);

/*
 * 检查扫描是否正在进行。
 */
bool bt_gamepad_is_scanning(void);

/*
 * 获取扫描结果列表。
 * 返回结果数量，results 数组由调用方提供。
 */
int bt_gamepad_get_scan_results(bt_gamepad_scan_result_t *results, int max_count);

/*
 * 连接指定扫描结果中的设备（按索引）。
 */
esp_err_t bt_gamepad_connect_by_index(int index);

/*
 * 蓝牙手柄是否已连接。
 */
bool bt_gamepad_is_connected(void);

/*
 * 获取已连接设备名称。
 */
const char *bt_gamepad_get_connected_name(void);

/*
 * ---- 按键学习模式 ----
 *
 * 学习流程：
 *   1. bt_gamepad_start_learn() 进入学习模式
 *   2. bt_gamepad_get_learn_key() 获取当前要学习的按键索引 (0~9)
 *   3. 主程序在屏幕上显示提示，等待用户在手柄上按下并松开目标键
 *   4. 每个学习项切换后，组件先等待固定防残留窗口；随后用户按住
 *      目标键触发第一份报告（D1），松开后用第二份不同报告确认（D2）
 *   5. 组件从 D1—D2 报告对提取单一按钮 bit 或 Hat 方向字段，不要求
 *      手柄在静止时持续上报空闲报告
 *   6. bt_gamepad_check_learned() 仅在完整按下—松开报告对确认后返回 true
 *   7. bt_gamepad_finish_learn() 保存字段级映射到 NVS
 */

/*
 * 进入按键学习模式（从第 0 个按键开始）。
 */
esp_err_t bt_gamepad_start_learn(void);

/*
 * 获取当前正在学习的按键索引 (0~9)，返回 -1 表示不在学习模式。
 */
int bt_gamepad_get_learn_key(void);

/*
 * 检查当前学习按键是否已完成捕获。
 * 只有接收到目标 HID 报告的完整按下—松开周期后才返回 true。
 *
 * 当前实现使用字段级映射，不再保存整包 raw report；保留 out_data/out_len
 * 仅为兼容已有调用方，成功时 out_len 被置为 0，out_data 不写入。
 */
bool bt_gamepad_check_learned(uint8_t *out_data, uint8_t max_len, uint8_t *out_len);

/*
 * 检查事件型学习模式是否已过防残留窗口、可接收新的按下事件。
 */
bool bt_gamepad_is_learn_ready(void);

/*
 * 跳过当前按键（标记为未映射，使用默认逻辑）。
 */
esp_err_t bt_gamepad_skip_learn_key(void);

/*
 * 完成学习模式，将字段级映射保存到 NVS。
 */
esp_err_t bt_gamepad_finish_learn(void);

/*
 * 从 NVS 加载已保存的字段级按键映射（bt_gamepad_start 内部会自动调用）。
 */
esp_err_t bt_gamepad_load_keymap(void);

/*
 * 检查是否有已保存的按键映射。
 */
bool bt_gamepad_has_saved_keymap(void);

/*
 * 当前已连接手柄是否与已保存的完整十键映射匹配。
 * 匹配以设备名称、HID appearance 和已保存报告字段共同判断；仅匹配时
 * 才应向用户提供“SELECT 跳过学习”的选项。
 */
bool bt_gamepad_has_compatible_saved_keymap(void);

/*
 * 将当前蓝牙手柄状态强制设为全松开。用于 Select/Start 菜单确认后，
 * 防止确认键被带入游戏或学习流程。
 */
void bt_gamepad_reset_joypad_state(void);

/*
 * 获取蓝牙手柄当前按键状态（8bit 低有效）。
 * 无手柄连接时返回 0xFF（全松开）。
 */
uint8_t bt_gamepad_get_joypad_state(void);

/*
 * 开机时尝试自动连接已配对的蓝牙手柄。
 * 仅当 NVS 中已保存完整的十键映射且设备指纹有效时才尝试。
 * 扫描 BLE 设备，按名称 + appearance 匹配，成功连接返回 ESP_OK。
 * 失败返回 ESP_FAIL，调用方应回退到手动配置流程。
 */
esp_err_t bt_gamepad_try_auto_connect(void);

#ifdef __cplusplus
}
#endif
