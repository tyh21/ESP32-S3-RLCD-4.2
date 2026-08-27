#include "bt_gamepad.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_hidh.h"
#include "esp_hid_common.h"

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bt_gamepad";

/*
 * 映射格式 v5。
 *
 * 旧版保存的是整包 raw input；这会把上一按键残留的报告、组合键报告和
 * 不同状态报告误当成独立按键。v3 只保存单一字段：一个按钮 bit 或一个
 * 标准 HID Hat 值。
 */
#define BT_GAMEPAD_NVS_NS   "btgame"
/*
 * keymap5 强制与 V8/V8.1 的十键记录隔离：旧记录可能是在反向极性
 * bug 存在时写入，不能直接进入运行时。
 */
#define BT_GAMEPAD_NVS_KEY  "keymap5"
#define BT_GAMEPAD_NVS_DEVICE_KEY "keymap5dev"
#define BT_GAMEPAD_DEVICE_FINGERPRINT_VERSION 1
#define BT_GAMEPAD_CONNECTED_NAME_MAX 32

/* valid + kind + map + rid(2) + len + byte + mask + active + idle */
#define BT_GAMEPAD_KEYMAP_BLOB_ENTRY_SIZE 10

/*
 * 此手柄只在状态变化时通知；项目切换后先丢弃旧键的延迟通知，
 * 然后把用户指定按键的“按下报告 + 松开报告”成对解析。
 */
#define BT_LEARN_EVENT_GUARD_MS        1000
#define BT_GAMEPAD_BUILD_ID             "FIELD_KEYMAP_REUSE_V10_20260818"

/* GB joypad bit 掩码（低有效） */
#define BIT_A      (1U << 0)
#define BIT_B      (1U << 1)
#define BIT_SEL    (1U << 2)
#define BIT_START  (1U << 3)
#define BIT_RIGHT  (1U << 4)
#define BIT_LEFT   (1U << 5)
#define BIT_UP     (1U << 6)
#define BIT_DOWN   (1U << 7)

/*
 * Game Boy 本体只有 A/B；为完整支持常见四面键蓝牙手柄，X 作为 A
 * 的兼容别名，Y 作为 B 的兼容别名。网页手柄本来只发送八位 GB 状态，
 * 因而无需修改 Web 协议；BLE X/Y 在此处归并到同一 A/B 位。
 */
const char *bt_gamepad_key_names[BT_GAMEPAD_NUM_KEYS] = {
    "A", "B", "X", "Y", "SELECT", "START",
    "RIGHT", "LEFT", "UP", "DOWN"
};

static const uint8_t s_key_bits[BT_GAMEPAD_NUM_KEYS] = {
    BIT_A, BIT_B, BIT_A, BIT_B, BIT_SEL, BIT_START,
    BIT_RIGHT, BIT_LEFT, BIT_UP, BIT_DOWN
};

typedef enum {
    BT_GAMEPAD_FIELD_NONE = 0,
    BT_GAMEPAD_FIELD_BUTTON_BIT = 1,
    BT_GAMEPAD_FIELD_HAT_VALUE = 2,
} bt_gamepad_field_kind_t;

/*
 * 一个 GB 键只对应 HID 报告中的一个字段。
 *
 * BUTTON_BIT:
 *   byte_index 指向按钮位图所在字节，mask 只有一位；active_value 是按下时
 *   该位的电平（0 表示低有效，mask 表示高有效）。
 *
 * HAT_VALUE:
 *   byte_index 指向方向帽字节，mask 通常为 0x0F；active_value 是该方向的
 *   标准 HID Hat 值（0=UP, 2=RIGHT, 4=DOWN, 6=LEFT）。
 */
typedef struct {
    bool valid;
    uint8_t kind;
    uint8_t map_index;
    uint16_t report_id;
    uint8_t report_len;
    uint8_t byte_index;
    uint8_t mask;
    uint8_t active_value;
    uint8_t idle_value;
} bt_gamepad_keymap_entry_t;

typedef struct {
    bool valid;
    uint8_t kind;
    uint8_t byte_index;
    uint8_t mask;
    uint8_t active_value;
    uint8_t idle_value;
} bt_gamepad_field_candidate_t;

/*
 * BLE 私有地址可能变化，故不保存 MAC。名称 + HID appearance 作为稳定的
 * 软指纹；字段映射中的 map/report-id/length 继续提供报告结构层的约束。
 */
typedef struct __attribute__((packed)) {
    uint8_t version;
    uint16_t appearance;
    char name[BT_GAMEPAD_CONNECTED_NAME_MAX];
} bt_gamepad_device_fingerprint_t;

/* ---- 运行时状态 ---- */
static volatile uint8_t s_joypad_state = 0xFF;
static volatile bool s_connected = false;
static char s_connected_name[BT_GAMEPAD_CONNECTED_NAME_MAX] = {0};
static uint16_t s_connected_appearance = 0;

/* ---- 扫描结果 ---- */
static bt_gamepad_scan_result_t s_scan_results[BT_GAMEPAD_MAX_SCAN_RESULTS];
static volatile int s_scan_count = 0;
static volatile bool s_scanning = false;

/* ---- 字段级映射 ---- */
static bt_gamepad_keymap_entry_t s_keymap[BT_GAMEPAD_NUM_KEYS];
static bool s_keymap_loaded = false;

/* 兼容映射公共查询依赖的内部存储辅助函数。 */
static bool bt_gamepad_keymap_is_complete(void);
static bool bt_gamepad_load_device_fingerprint(bt_gamepad_device_fingerprint_t *fingerprint);

/* ---- 事件型学习状态 ---- */
static volatile bool s_learn_mode = false;
static volatile int s_learn_key_index = -1;
static volatile bool s_learn_captured = false;
/* D1: 已过防残留窗口、等待用户按下；D2: 已收到按下、等待松开。 */
static volatile bool s_learn_armed = false;
static volatile bool s_learn_pending = false;
static volatile int64_t s_learn_next_capture_us = 0;

/* D2 中保存用户第一次（按下）通知；第二份不同报告用于确认字段恢复。 */
static uint8_t s_learn_pressed_report[BT_GAMEPAD_MAX_RAW_LEN];
static uint8_t s_learn_pressed_len = 0;
static uint8_t s_learn_pressed_map_index = 0;
static uint16_t s_learn_pressed_report_id = 0;

/* 最近一份已确认的报告，用作下一项 D1 的空闲参照。 */
static uint8_t s_learn_last_report[BT_GAMEPAD_MAX_RAW_LEN];
static uint8_t s_learn_last_len = 0;
static uint8_t s_learn_last_map_index = 0;
static uint16_t s_learn_last_report_id = 0;
static bool s_learn_last_valid = false;

/* 已锁定、等待当前字段恢复的候选。 */
static bt_gamepad_field_candidate_t s_learn_candidate;
static bool s_learn_candidate_from_last = false;

/* ============================================================
 * 通用辅助函数
 * ============================================================ */

static uint8_t bt_gamepad_popcount8(uint8_t value)
{
    uint8_t count = 0;
    while (value != 0) {
        count += value & 1U;
        value >>= 1;
    }
    return count;
}

static void bt_gamepad_format_report(const uint8_t *data, uint8_t len,
                                     char *out, size_t out_size)
{
    size_t pos = 0;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';

    for (uint8_t i = 0; i < len && pos + 4 < out_size; i++) {
        int written = snprintf(out + pos, out_size - pos,
                               "%02X%s", data[i], (i + 1U < len) ? " " : "");
        if (written < 0) {
            break;
        }
        pos += (size_t)written;
    }
}

static void bt_gamepad_clear_scan_results(void)
{
    memset(s_scan_results, 0, sizeof(s_scan_results));
    s_scan_count = 0;
}

static int bt_gamepad_find_or_add_scan_slot(const uint8_t *bda)
{
    for (int i = 0; i < s_scan_count; i++) {
        if (s_scan_results[i].used &&
            memcmp(s_scan_results[i].bda, bda, 6) == 0) {
            return i;
        }
    }

    if (s_scan_count < BT_GAMEPAD_MAX_SCAN_RESULTS) {
        return s_scan_count++;
    }
    return -1;
}

/*
 * 该 10-byte Gamepad 报告的 0~3 是模拟摇杆轴；byte9 在真机日志中会在
 * 0x00/0x02 间异步切换，不代表某个独立物理按键。二者均不参与学习。
 */
static bool bt_gamepad_is_ignored_learning_byte(uint8_t index, uint8_t report_len)
{
    return (report_len >= 8 && index < 4) || (report_len >= 10 && index == 9);
}

static bool bt_gamepad_keymap_entry_is_valid(const bt_gamepad_keymap_entry_t *entry)
{
    if (entry == NULL || !entry->valid ||
        (entry->kind != BT_GAMEPAD_FIELD_BUTTON_BIT &&
         entry->kind != BT_GAMEPAD_FIELD_HAT_VALUE) ||
        entry->report_len == 0 || entry->report_len > BT_GAMEPAD_MAX_RAW_LEN ||
        entry->byte_index >= entry->report_len || entry->mask == 0) {
        return false;
    }

    if (entry->kind == BT_GAMEPAD_FIELD_BUTTON_BIT) {
        uint8_t active = entry->active_value & entry->mask;
        uint8_t idle = entry->idle_value & entry->mask;
        /* 普通按钮必须恰有一位、且按下与松开电平不同。 */
        return bt_gamepad_popcount8(entry->mask) == 1 && active != idle;
    }

    uint8_t active_hat = entry->active_value & 0x0F;
    uint8_t idle_hat = entry->idle_value & 0x0F;
    /* Hat 方向必须是 0~7；空闲值仅接受标准 8/F。 */
    return active_hat <= 0x07 && (idle_hat == 0x08 || idle_hat == 0x0F) &&
           active_hat != idle_hat;
}

static bool bt_gamepad_report_identity_matches(const bt_gamepad_keymap_entry_t *entry,
                                               uint8_t map_index,
                                               uint16_t report_id,
                                               uint16_t length)
{
    return bt_gamepad_keymap_entry_is_valid(entry) &&
           entry->map_index == map_index && entry->report_id == report_id &&
           entry->report_len == length;
}

static bool bt_gamepad_is_standard_hat_transition(uint8_t idle_value,
                                                   uint8_t pressed_value)
{
    uint8_t idle_nibble = idle_value & 0x0F;
    uint8_t pressed_nibble = pressed_value & 0x0F;

    /*
     * HID Hat Switch：低四位 0~7 为八个方向，8 或 F 通常为中立。
     * 本手柄的中立值是 0xFF，而方向值是 0x00~0x07；高四位并不保持
     * 不变，因此只能比较低四位方向码。
     */
    return (idle_nibble == 0x08 || idle_nibble == 0x0F) &&
           pressed_nibble <= 0x07;
}

/*
 * 从用户指定同一按键的“按下报告 -> 松开报告”中提取字段。
 *
 * 事件型手柄在静止时不上报，不能预先等待空闲基线。界面提示后第一份
 * 报告视为按下，下一份不同报告视为松开；两者之间必须只有一个可解释的
 * 按钮 bit 或标准 Hat 方向变化，才保存为映射。
 */
static bool bt_gamepad_find_field_from_pair(const uint8_t *pressed,
                                            const uint8_t *released,
                                            uint16_t length,
                                            bt_gamepad_field_candidate_t *candidate)
{
    int changed_byte = -1;
    uint8_t changed_bits = 0;
    uint8_t changed_bytes = 0;
    uint8_t changed_mask = 0;

    if (pressed == NULL || released == NULL || candidate == NULL ||
        length == 0 || length > BT_GAMEPAD_MAX_RAW_LEN) {
        return false;
    }

    memset(candidate, 0, sizeof(*candidate));
    for (uint8_t i = 0; i < length; i++) {
        /* 前四字节是模拟轴，按键学习中不作为候选。 */
        if (bt_gamepad_is_ignored_learning_byte(i, (uint8_t)length)) {
            continue;
        }

        uint8_t diff = pressed[i] ^ released[i];
        if (diff == 0) {
            continue;
        }
        changed_bytes++;
        changed_byte = i;
        changed_mask = diff;
        changed_bits = (uint8_t)(changed_bits + bt_gamepad_popcount8(diff));
    }

    if (changed_bits == 1 && changed_byte >= 0) {
        candidate->valid = true;
        candidate->kind = BT_GAMEPAD_FIELD_BUTTON_BIT;
        candidate->byte_index = (uint8_t)changed_byte;
        candidate->mask = changed_mask;
        candidate->active_value = pressed[changed_byte] & changed_mask;
        candidate->idle_value = released[changed_byte] & changed_mask;
        return true;
    }

    if (changed_bytes == 1 && changed_byte >= 0 &&
        bt_gamepad_is_standard_hat_transition(
            released[changed_byte], pressed[changed_byte])) {
        candidate->valid = true;
        candidate->kind = BT_GAMEPAD_FIELD_HAT_VALUE;
        candidate->byte_index = (uint8_t)changed_byte;
        candidate->mask = 0x0F;
        candidate->active_value = pressed[changed_byte] & 0x0F;
        candidate->idle_value = released[changed_byte] & 0x0F;
        return true;
    }

    return false;
}

static bool bt_gamepad_reports_equal_for_learning(const uint8_t *a, const uint8_t *b,
                                                  uint16_t length)
{
    if (a == NULL || b == NULL || length == 0 || length > BT_GAMEPAD_MAX_RAW_LEN) {
        return false;
    }
    for (uint8_t i = 0; i < length; i++) {
        if (!bt_gamepad_is_ignored_learning_byte(i, (uint8_t)length) && a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static bool bt_gamepad_candidate_is_released(const bt_gamepad_field_candidate_t *candidate,
                                             const uint8_t *data, uint16_t length)
{
    if (candidate == NULL || !candidate->valid || data == NULL ||
        candidate->byte_index >= length) {
        return false;
    }
    return (data[candidate->byte_index] & candidate->mask) == candidate->idle_value;
}

/* 除锁定候选和已知状态字段外，其他有效字段也变化时不能确认当前键松开。 */
static bool bt_gamepad_other_fields_unchanged(const uint8_t *pressed,
                                              const uint8_t *current,
                                              uint16_t length,
                                              const bt_gamepad_field_candidate_t *candidate)
{
    if (pressed == NULL || current == NULL || candidate == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < length; i++) {
        if (i == candidate->byte_index ||
            bt_gamepad_is_ignored_learning_byte(i, (uint8_t)length)) {
            continue;
        }
        if (pressed[i] != current[i]) {
            return false;
        }
    }
    return true;
}

static bool bt_gamepad_candidate_is_duplicate(const bt_gamepad_field_candidate_t *candidate,
                                              uint8_t map_index, uint16_t report_id,
                                              uint16_t report_len)
{
    if (candidate == NULL || !candidate->valid) {
        return false;
    }

    for (int i = 0; i < s_learn_key_index; i++) {
        const bt_gamepad_keymap_entry_t *entry = &s_keymap[i];
        if (!entry->valid || entry->map_index != map_index ||
            entry->report_id != report_id || entry->report_len != report_len) {
            continue;
        }
        if (entry->kind == candidate->kind &&
            entry->byte_index == candidate->byte_index &&
            entry->mask == candidate->mask &&
            entry->active_value == candidate->active_value) {
            return true;
        }
    }
    return false;
}

static bool bt_gamepad_hat_contains_direction(uint8_t learned_value,
                                              uint8_t current_value)
{
    learned_value &= 0x0F;
    current_value &= 0x0F;

    if (current_value == learned_value) {
        return true;
    }

    /* 标准 HID Hat 对角值同时包含相邻两个基准方向。 */
    switch (learned_value) {
    case 0: /* UP: UP-LEFT(7), UP-RIGHT(1) */
        return current_value == 1 || current_value == 7;
    case 2: /* RIGHT: UP-RIGHT(1), DOWN-RIGHT(3) */
        return current_value == 1 || current_value == 3;
    case 4: /* DOWN: DOWN-RIGHT(3), DOWN-LEFT(5) */
        return current_value == 3 || current_value == 5;
    case 6: /* LEFT: DOWN-LEFT(5), UP-LEFT(7) */
        return current_value == 5 || current_value == 7;
    default:
        return false;
    }
}

static bool bt_gamepad_field_is_idle(const bt_gamepad_keymap_entry_t *entry,
                                     const uint8_t *data, uint16_t length)
{
    if (!bt_gamepad_keymap_entry_is_valid(entry) || data == NULL ||
        entry->byte_index >= length) {
        return false;
    }

    uint8_t field_value = data[entry->byte_index] & entry->mask;
    if (entry->kind == BT_GAMEPAD_FIELD_BUTTON_BIT) {
        return field_value == (entry->idle_value & entry->mask);
    }
    return (field_value & 0x0F) == (entry->idle_value & 0x0F);
}

static bool bt_gamepad_field_is_pressed(const bt_gamepad_keymap_entry_t *entry,
                                        const uint8_t *data, uint16_t length)
{
    if (!bt_gamepad_keymap_entry_is_valid(entry) || data == NULL ||
        entry->byte_index >= length) {
        return false;
    }

    uint8_t field_value = data[entry->byte_index] & entry->mask;

    if (entry->kind == BT_GAMEPAD_FIELD_BUTTON_BIT) {
        uint8_t active = entry->active_value & entry->mask;
        uint8_t idle = entry->idle_value & entry->mask;
        /* active 与 idle 相同的条目绝不允许把空闲报告判成按下。 */
        return active != idle && field_value == active && field_value != idle;
    }
    if (entry->kind == BT_GAMEPAD_FIELD_HAT_VALUE) {
        uint8_t active_hat = entry->active_value & 0x0F;
        uint8_t idle_hat = entry->idle_value & 0x0F;
        return active_hat != idle_hat &&
               bt_gamepad_hat_contains_direction(active_hat, field_value);
    }
    return false;
}

static uint8_t bt_gamepad_apply_keymap(const uint8_t *data, uint16_t length,
                                        uint8_t map_index, uint16_t report_id)
{
    uint8_t state = 0xFF;

    for (int i = 0; i < BT_GAMEPAD_NUM_KEYS; i++) {
        const bt_gamepad_keymap_entry_t *entry = &s_keymap[i];
        if (!bt_gamepad_report_identity_matches(entry, map_index, report_id, length)) {
            continue;
        }
        if (bt_gamepad_field_is_pressed(entry, data, length)) {
            state &= (uint8_t)~s_key_bits[i];
        }
    }

    return state;
}

static void bt_gamepad_store_current_candidate(void)
{
    bt_gamepad_keymap_entry_t *entry = &s_keymap[s_learn_key_index];

    memset(entry, 0, sizeof(*entry));
    entry->valid = s_learn_candidate.valid;
    entry->kind = s_learn_candidate.kind;
    entry->map_index = s_learn_pressed_map_index;
    entry->report_id = s_learn_pressed_report_id;
    entry->report_len = s_learn_pressed_len;
    entry->byte_index = s_learn_candidate.byte_index;
    entry->mask = s_learn_candidate.mask;
    entry->active_value = s_learn_candidate.active_value;
    entry->idle_value = s_learn_candidate.idle_value;
}

/*
 * 最后一个学习项目在 D3 触发时的报告应为“所有键已松开”的可信状态。
 * 若某按钮的候选极性被事件顺序反转，空闲字段会等于 active 而非 idle；
 * 这里自动交换该位，防止进入游戏后未按键就触发 A/X 等别名输出。
 */
static void bt_gamepad_normalize_keymap_against_final_idle(void)
{
    if (!s_learn_last_valid) {
        return;
    }

    for (int i = 0; i < BT_GAMEPAD_NUM_KEYS; i++) {
        bt_gamepad_keymap_entry_t *entry = &s_keymap[i];
        if (!bt_gamepad_report_identity_matches(entry, s_learn_last_map_index,
                                                s_learn_last_report_id,
                                                s_learn_last_len)) {
            continue;
        }

        if (bt_gamepad_field_is_idle(entry, s_learn_last_report,
                                     s_learn_last_len)) {
            continue;
        }

        if (entry->kind == BT_GAMEPAD_FIELD_BUTTON_BIT) {
            uint8_t field_value = s_learn_last_report[entry->byte_index] & entry->mask;
            uint8_t active = entry->active_value & entry->mask;
            if (field_value == active) {
                uint8_t saved_active = entry->active_value;
                entry->active_value = entry->idle_value;
                entry->idle_value = saved_active;
                ESP_LOGW(TAG, "修正反向学习极性 %s: active=0x%02X idle=0x%02X",
                         bt_gamepad_key_names[i], (unsigned)entry->active_value,
                         (unsigned)entry->idle_value);
                continue;
            }
        }

        ESP_LOGW(TAG, "禁用非空闲映射 %s，避免游戏内误触发", bt_gamepad_key_names[i]);
        entry->valid = false;
    }
}

/* ============================================================
 * NVS 映射存取
 * ============================================================ */

esp_err_t bt_gamepad_load_keymap(void)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(BT_GAMEPAD_NVS_NS, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t buf[BT_GAMEPAD_NUM_KEYS * BT_GAMEPAD_KEYMAP_BLOB_ENTRY_SIZE];
    size_t size = sizeof(buf);
    ret = nvs_get_blob(h, BT_GAMEPAD_NVS_KEY, buf, &size);
    nvs_close(h);

    if (ret != ESP_OK || size != sizeof(buf)) {
        ESP_LOGI(TAG, "没有可用的字段级按键映射");
        return (ret == ESP_OK) ? ESP_ERR_INVALID_SIZE : ret;
    }

    memset(s_keymap, 0, sizeof(s_keymap));
    const uint8_t *p = buf;
    for (int i = 0; i < BT_GAMEPAD_NUM_KEYS; i++) {
        bt_gamepad_keymap_entry_t *entry = &s_keymap[i];
        entry->valid = (*p++ != 0);
        entry->kind = *p++;
        entry->map_index = *p++;
        entry->report_id = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        p += 2;
        entry->report_len = *p++;
        entry->byte_index = *p++;
        entry->mask = *p++;
        entry->active_value = *p++;
        entry->idle_value = *p++;

        if (!bt_gamepad_keymap_entry_is_valid(entry)) {
            ESP_LOGW(TAG, "忽略无效映射 %s: kind=%u byte=%u mask=0x%02X active=0x%02X idle=0x%02X",
                     bt_gamepad_key_names[i], (unsigned)entry->kind,
                     (unsigned)entry->byte_index, (unsigned)entry->mask,
                     (unsigned)entry->active_value, (unsigned)entry->idle_value);
            entry->valid = false;
        }
    }

    s_keymap_loaded = true;
        ESP_LOGI(TAG, "十键字段级按键映射已从 NVS 加载（X->A, Y->B）");
    for (int i = 0; i < BT_GAMEPAD_NUM_KEYS; i++) {
        const bt_gamepad_keymap_entry_t *entry = &s_keymap[i];
        if (entry->valid) {
            ESP_LOGI(TAG, "  %s: %s byte=%u mask=0x%02X active=0x%02X idle=0x%02X",
                     bt_gamepad_key_names[i],
                     entry->kind == BT_GAMEPAD_FIELD_HAT_VALUE ? "HAT" : "BIT",
                     (unsigned)entry->byte_index, (unsigned)entry->mask,
                     (unsigned)entry->active_value, (unsigned)entry->idle_value);
        }
    }
    return ESP_OK;
}

bool bt_gamepad_has_saved_keymap(void)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(BT_GAMEPAD_NVS_NS, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        return false;
    }

    size_t size = 0;
    ret = nvs_get_blob(h, BT_GAMEPAD_NVS_KEY, NULL, &size);
    nvs_close(h);
    return ret == ESP_OK &&
           size == BT_GAMEPAD_NUM_KEYS * BT_GAMEPAD_KEYMAP_BLOB_ENTRY_SIZE;
}

bool bt_gamepad_has_compatible_saved_keymap(void)
{
    if (!s_connected || !s_keymap_loaded || !bt_gamepad_keymap_is_complete()) {
        return false;
    }

    bt_gamepad_device_fingerprint_t fingerprint = {0};
    if (!bt_gamepad_load_device_fingerprint(&fingerprint)) {
        ESP_LOGI(TAG, "已保存映射缺少设备指纹，需重新学习一次");
        return false;
    }

    bool name_matches = strcmp(fingerprint.name, s_connected_name) == 0;
    bool appearance_matches = fingerprint.appearance == s_connected_appearance;
    if (!name_matches || !appearance_matches) {
        ESP_LOGI(TAG, "已保存映射不适用于当前手柄：saved=%s/0x%04X current=%s/0x%04X",
                 fingerprint.name, fingerprint.appearance,
                 s_connected_name, s_connected_appearance);
        return false;
    }

    ESP_LOGI(TAG, "发现与当前手柄兼容的已保存十键映射：%s", s_connected_name);
    return true;
}

void bt_gamepad_reset_joypad_state(void)
{
    s_joypad_state = 0xFF;
}

static bool bt_gamepad_keymap_is_complete(void)
{
    for (int i = 0; i < BT_GAMEPAD_NUM_KEYS; i++) {
        if (!bt_gamepad_keymap_entry_is_valid(&s_keymap[i])) {
            return false;
        }
    }
    return true;
}

static esp_err_t bt_gamepad_save_device_fingerprint(nvs_handle_t h)
{
    bt_gamepad_device_fingerprint_t fingerprint = {
        .version = BT_GAMEPAD_DEVICE_FINGERPRINT_VERSION,
        .appearance = s_connected_appearance,
    };
    strlcpy(fingerprint.name, s_connected_name, sizeof(fingerprint.name));
    return nvs_set_blob(h, BT_GAMEPAD_NVS_DEVICE_KEY, &fingerprint, sizeof(fingerprint));
}

static bool bt_gamepad_load_device_fingerprint(bt_gamepad_device_fingerprint_t *fingerprint)
{
    if (fingerprint == NULL) {
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(BT_GAMEPAD_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(*fingerprint);
    esp_err_t ret = nvs_get_blob(h, BT_GAMEPAD_NVS_DEVICE_KEY, fingerprint, &size);
    nvs_close(h);
    return ret == ESP_OK && size == sizeof(*fingerprint) &&
           fingerprint->version == BT_GAMEPAD_DEVICE_FINGERPRINT_VERSION &&
           fingerprint->name[0] != '\0';
}

static esp_err_t bt_gamepad_save_keymap(void)
{
    uint8_t buf[BT_GAMEPAD_NUM_KEYS * BT_GAMEPAD_KEYMAP_BLOB_ENTRY_SIZE];
    uint8_t *p = buf;

    for (int i = 0; i < BT_GAMEPAD_NUM_KEYS; i++) {
        const bt_gamepad_keymap_entry_t *entry = &s_keymap[i];
        *p++ = entry->valid ? 1 : 0;
        *p++ = entry->kind;
        *p++ = entry->map_index;
        *p++ = (uint8_t)(entry->report_id & 0xFF);
        *p++ = (uint8_t)(entry->report_id >> 8);
        *p++ = entry->report_len;
        *p++ = entry->byte_index;
        *p++ = entry->mask;
        *p++ = entry->active_value;
        *p++ = entry->idle_value;
    }

    nvs_handle_t h;
    esp_err_t ret = nvs_open(BT_GAMEPAD_NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(h, BT_GAMEPAD_NVS_KEY, buf, sizeof(buf));
    if (ret == ESP_OK) {
        ret = bt_gamepad_save_device_fingerprint(h);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(h);
    }
    nvs_close(h);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "十键字段级按键映射及设备指纹已保存到 NVS（X->A, Y->B）");
    } else {
        ESP_LOGE(TAG, "保存字段级按键映射失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ============================================================
 * HID 输入回调
 * ============================================================ */

static void bt_gamepad_learn_consume_report(const uint8_t *data, uint16_t len,
                                            uint8_t map_index, uint16_t report_id,
                                            esp_hid_usage_t usage)
{
    bool is_gamepad_report = usage == ESP_HID_USAGE_GAMEPAD ||
                             usage == ESP_HID_USAGE_JOYSTICK;
    int64_t now_us = esp_timer_get_time();

    if (!s_learn_mode || s_learn_key_index < 0 ||
        s_learn_key_index >= BT_GAMEPAD_NUM_KEYS || !is_gamepad_report ||
        data == NULL || len == 0 || len > BT_GAMEPAD_MAX_RAW_LEN ||
        s_learn_captured) {
        return;
    }

    /* D1：窗口到期后，从上一份已确认报告锁定本次按下的唯一字段。 */
    if (!s_learn_pending) {
        if (now_us < s_learn_next_capture_us) {
            return;
        }

        bool same_identity_as_last = s_learn_last_valid &&
            len == s_learn_last_len && map_index == s_learn_last_map_index &&
            report_id == s_learn_last_report_id;

        /* byte9 等状态字段单独变化不代表按键，不把它记录成 D1。 */
        if (same_identity_as_last &&
            bt_gamepad_reports_equal_for_learning(data, s_learn_last_report, len)) {
            memcpy(s_learn_last_report, data, len);
            return;
        }

        bt_gamepad_field_candidate_t candidate;
        bool candidate_from_last = same_identity_as_last &&
            bt_gamepad_find_field_from_pair(data, s_learn_last_report, len, &candidate);

        if (candidate_from_last &&
            bt_gamepad_candidate_is_duplicate(&candidate, map_index, report_id, len)) {
            ESP_LOGW(TAG, "D1 丢弃已学习字段，仍等待 %s: byte=%u mask=0x%02X",
                     bt_gamepad_key_names[s_learn_key_index],
                     (unsigned)candidate.byte_index, (unsigned)candidate.mask);
            /* 保留旧项的松开报告；迟到按下状态不能成为下一项基准。 */
            s_learn_next_capture_us = now_us + (int64_t)BT_LEARN_EVENT_GUARD_MS * 1000;
            return;
        }

        memcpy(s_learn_pressed_report, data, len);
        s_learn_pressed_len = (uint8_t)len;
        s_learn_pressed_map_index = map_index;
        s_learn_pressed_report_id = report_id;
        s_learn_candidate_from_last = candidate_from_last;
        if (candidate_from_last) {
            s_learn_candidate = candidate;
        } else {
            memset(&s_learn_candidate, 0, sizeof(s_learn_candidate));
        }
        s_learn_pending = true;
        s_learn_armed = false;

        char report_text[BT_GAMEPAD_MAX_RAW_LEN * 3 + 1];
        bt_gamepad_format_report(data, (uint8_t)len, report_text, sizeof(report_text));
        if (candidate_from_last) {
            ESP_LOGI(TAG, "D1 已锁定 %s: %s byte=%u mask=0x%02X active=0x%02X raw=[%s]；请松开",
                     bt_gamepad_key_names[s_learn_key_index],
                     candidate.kind == BT_GAMEPAD_FIELD_HAT_VALUE ? "HAT" : "BIT",
                     (unsigned)candidate.byte_index, (unsigned)candidate.mask,
                     (unsigned)candidate.active_value, report_text);
        } else {
            ESP_LOGI(TAG, "D1 已记录 %s 按下报告（无上一基线）: raw=[%s]；请松开",
                     bt_gamepad_key_names[s_learn_key_index], report_text);
        }
        return;
    }

    /* D2：只处理与 D1 相同报告身份的后续事件。 */
    if (len != s_learn_pressed_len || map_index != s_learn_pressed_map_index ||
        report_id != s_learn_pressed_report_id ||
        bt_gamepad_reports_equal_for_learning(data, s_learn_pressed_report, len)) {
        return;
    }

    bt_gamepad_field_candidate_t candidate = s_learn_candidate;
    if (s_learn_candidate_from_last) {
        /* 只认当前锁定字段回到 idle；其他按键变动不构成“松开”。 */
        if (!bt_gamepad_candidate_is_released(&candidate, data, len) ||
            !bt_gamepad_other_fields_unchanged(s_learn_pressed_report, data, len,
                                               &candidate)) {
            return;
        }
    } else {
        /* 首个按键没有上一报告时，退回同一按下—松开报告对提取一次。 */
        if (!bt_gamepad_find_field_from_pair(s_learn_pressed_report, data, len,
                                             &candidate)) {
            char press_text[BT_GAMEPAD_MAX_RAW_LEN * 3 + 1];
            char release_text[BT_GAMEPAD_MAX_RAW_LEN * 3 + 1];
            bt_gamepad_format_report(s_learn_pressed_report, s_learn_pressed_len,
                                     press_text, sizeof(press_text));
            bt_gamepad_format_report(data, (uint8_t)len, release_text, sizeof(release_text));
            ESP_LOGW(TAG, "D2 无法从首对报告提取 %s: down=[%s] up=[%s]",
                     bt_gamepad_key_names[s_learn_key_index], press_text, release_text);
            s_learn_pending = false;
            s_learn_next_capture_us = now_us + (int64_t)BT_LEARN_EVENT_GUARD_MS * 1000;
            return;
        }
        if (bt_gamepad_candidate_is_duplicate(&candidate, map_index, report_id, len)) {
            ESP_LOGW(TAG, "D2 字段与已学习键重复，仍等待 %s: byte=%u mask=0x%02X",
                     bt_gamepad_key_names[s_learn_key_index],
                     (unsigned)candidate.byte_index, (unsigned)candidate.mask);
            s_learn_pending = false;
            s_learn_next_capture_us = now_us + (int64_t)BT_LEARN_EVENT_GUARD_MS * 1000;
            return;
        }
    }

    /* D3：当前锁定字段已恢复，保存映射并将此次松开报告作为下一项参照。 */
    s_learn_candidate = candidate;
    s_learn_pending = false;
    s_learn_captured = true;
    memcpy(s_learn_last_report, data, len);
    s_learn_last_len = (uint8_t)len;
    s_learn_last_map_index = map_index;
    s_learn_last_report_id = report_id;
    s_learn_last_valid = true;

    char press_text[BT_GAMEPAD_MAX_RAW_LEN * 3 + 1];
    char release_text[BT_GAMEPAD_MAX_RAW_LEN * 3 + 1];
    bt_gamepad_format_report(s_learn_pressed_report, s_learn_pressed_len,
                             press_text, sizeof(press_text));
    bt_gamepad_format_report(data, (uint8_t)len, release_text, sizeof(release_text));
    ESP_LOGI(TAG, "D3 确认 %s: %s byte=%u mask=0x%02X idle=0x%02X active=0x%02X down=[%s] up=[%s]",
             bt_gamepad_key_names[s_learn_key_index],
             candidate.kind == BT_GAMEPAD_FIELD_HAT_VALUE ? "HAT" : "BIT",
             (unsigned)candidate.byte_index, (unsigned)candidate.mask,
             (unsigned)candidate.idle_value, (unsigned)candidate.active_value,
             press_text, release_text);
}

static void bt_gamepad_hidh_callback(void *handler_args, esp_event_base_t base,
                                     int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDH_OPEN_EVENT:
        if (param->open.status == ESP_OK) {
            s_connected = true;
            const char *name = esp_hidh_dev_name_get(param->open.dev);
            strlcpy(s_connected_name, name ? name : "unknown",
                    sizeof(s_connected_name));
            ESP_LOGI(TAG, "手柄已连接：%s", s_connected_name);
        } else {
            ESP_LOGE(TAG, "手柄连接失败");
        }
        break;

    case ESP_HIDH_INPUT_EVENT: {
        const uint8_t *data = param->input.data;
        uint16_t len = param->input.length;
        uint8_t map_index = param->input.map_index;
        uint16_t report_id = param->input.report_id;

        if (data == NULL || len == 0 || len > BT_GAMEPAD_MAX_RAW_LEN) {
            break;
        }

        if (s_learn_mode) {
            bt_gamepad_learn_consume_report(data, len, map_index, report_id,
                                            param->input.usage);
        } else if (s_keymap_loaded) {
            s_joypad_state = bt_gamepad_apply_keymap(data, len,
                                                      map_index, report_id);
        }
        break;
    }

    case ESP_HIDH_CLOSE_EVENT:
        ESP_LOGI(TAG, "手柄已断开");
        s_connected = false;
        s_joypad_state = 0xFF;
        s_connected_name[0] = '\0';
        break;

    case ESP_HIDH_BATTERY_EVENT:
        ESP_LOGI(TAG, "手柄电量：%d%%", param->battery.level);
        break;

    default:
        break;
    }
}

/* ============================================================
 * GAP 回调（BLE 扫描）
 * ============================================================ */

static void bt_gamepad_gap_ble_cb(esp_gap_ble_cb_event_t event,
                                   esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            uint8_t uuid_len = 0;
            uint8_t *uuid_data = esp_ble_resolve_adv_data_by_type(
                param->scan_rst.ble_adv,
                param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len,
                ESP_BLE_AD_TYPE_16SRV_CMPL, &uuid_len);
            if (uuid_data == NULL) {
                uuid_data = esp_ble_resolve_adv_data_by_type(
                    param->scan_rst.ble_adv,
                    param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len,
                    ESP_BLE_AD_TYPE_16SRV_PART, &uuid_len);
            }
            uint16_t uuid = uuid_data != NULL && uuid_len >= 2 ?
                            (uint16_t)uuid_data[0] | ((uint16_t)uuid_data[1] << 8) : 0;

            uint8_t appearance_len = 0;
            uint8_t *appearance_data = esp_ble_resolve_adv_data_by_type(
                param->scan_rst.ble_adv,
                param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len,
                ESP_BLE_AD_TYPE_APPEARANCE, &appearance_len);
            uint16_t appearance = appearance_data != NULL && appearance_len >= 2 ?
                                  (uint16_t)appearance_data[0] |
                                  ((uint16_t)appearance_data[1] << 8) : 0;

            if (uuid != 0x1812 && appearance != ESP_HID_APPEARANCE_GAMEPAD &&
                appearance != ESP_HID_APPEARANCE_JOYSTICK) {
                break;
            }

            int slot = bt_gamepad_find_or_add_scan_slot(param->scan_rst.bda);
            if (slot < 0) {
                break;
            }

            bt_gamepad_scan_result_t *result = &s_scan_results[slot];
            result->used = true;
            memcpy(result->bda, param->scan_rst.bda, sizeof(result->bda));
            result->addr_type = param->scan_rst.ble_addr_type;
            result->appearance = appearance;

            uint8_t name_len = 0;
            uint8_t *name_data = esp_ble_resolve_adv_data_by_type(
                param->scan_rst.ble_adv,
                param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len,
                ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
            if (name_data == NULL) {
                name_data = esp_ble_resolve_adv_data_by_type(
                    param->scan_rst.ble_adv,
                    param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len,
                    ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
            }
            if (name_data != NULL && name_len > 0) {
                size_t copy_len = name_len < sizeof(result->name) - 1 ?
                                  name_len : sizeof(result->name) - 1;
                memcpy(result->name, name_data, copy_len);
                result->name[copy_len] = '\0';
            } else {
                snprintf(result->name, sizeof(result->name), "BLE-%02X%02X",
                         result->bda[4], result->bda[5]);
            }

            ESP_LOGI(TAG, "发现 BLE HID: [%d] %s appearance=0x%04x",
                     slot, result->name, appearance);
        } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            s_scanning = false;
            ESP_LOGI(TAG, "BLE 扫描完成，共发现 %d 个 HID 设备", s_scan_count);
        }
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        s_scanning = false;
        break;

    default:
        break;
    }
}

/* ============================================================
 * 扫描、连接与学习公共接口
 * ============================================================ */

esp_err_t bt_gamepad_start_scan(void)
{
    bt_gamepad_clear_scan_results();
    s_scanning = true;

    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE,
    };

    esp_err_t ret = esp_ble_gap_set_scan_params(&scan_params);
    if (ret != ESP_OK) {
        s_scanning = false;
        return ret;
    }

    ret = esp_ble_gap_start_scanning(5);
    if (ret != ESP_OK) {
        s_scanning = false;
        return ret;
    }

    ESP_LOGI(TAG, "开始扫描 BLE HID 设备（5 秒）");
    return ESP_OK;
}

bool bt_gamepad_is_scanning(void)
{
    return s_scanning;
}

int bt_gamepad_get_scan_results(bt_gamepad_scan_result_t *results, int max_count)
{
    if (results == NULL || max_count <= 0) {
        return 0;
    }

    int count = s_scan_count < max_count ? s_scan_count : max_count;
    for (int i = 0; i < count; i++) {
        memcpy(&results[i], &s_scan_results[i], sizeof(results[i]));
    }
    return count;
}

esp_err_t bt_gamepad_connect_by_index(int index)
{
    if (index < 0 || index >= s_scan_count || !s_scan_results[index].used) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 连接前保存扫描得到的 appearance，学习完成时与设备名一起写入 NVS。 */
    s_connected_appearance = s_scan_results[index].appearance;
    esp_hidh_dev_t *device = esp_hidh_dev_open(s_scan_results[index].bda,
                                               ESP_HID_TRANSPORT_BLE,
                                               s_scan_results[index].addr_type);
    if (device == NULL) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "请求连接 BLE HID: %s", s_scan_results[index].name);
    return ESP_OK;
}

esp_err_t bt_gamepad_start_learn(void)
{
    /* 每轮均从干净映射开始，避免异常中断的一轮污染下一轮。 */
    memset(s_keymap, 0, sizeof(s_keymap));
    s_keymap_loaded = false;

    s_learn_mode = true;
    s_learn_key_index = 0;
    s_learn_captured = false;
    s_learn_armed = false;
    s_learn_pending = false;
    s_learn_next_capture_us = esp_timer_get_time() +
                              (int64_t)BT_LEARN_EVENT_GUARD_MS * 1000;
    s_learn_pressed_len = 0;
    s_learn_pressed_map_index = 0;
    s_learn_pressed_report_id = 0;
    memset(s_learn_pressed_report, 0, sizeof(s_learn_pressed_report));
    s_learn_last_len = 0;
    s_learn_last_map_index = 0;
    s_learn_last_report_id = 0;
    s_learn_last_valid = false;
    memset(s_learn_last_report, 0, sizeof(s_learn_last_report));
    memset(&s_learn_candidate, 0, sizeof(s_learn_candidate));
    s_learn_candidate_from_last = false;

    ESP_LOGI(TAG, "进入锁定字段学习：%d ms 后短按 A，看到 D1 后松开",
             BT_LEARN_EVENT_GUARD_MS);
    return ESP_OK;
}

int bt_gamepad_get_learn_key(void)
{
    if (!s_learn_mode || s_learn_key_index < 0 ||
        s_learn_key_index >= BT_GAMEPAD_NUM_KEYS) {
        return -1;
    }
    return s_learn_key_index;
}

bool bt_gamepad_check_learned(uint8_t *out_data, uint8_t max_len, uint8_t *out_len)
{
    if (!s_learn_mode || s_learn_key_index < 0 || !s_learn_captured) {
        return false;
    }

    /* 兼容旧接口：字段学习没有再保存完整 raw 报告。 */
    if (out_len != NULL) {
        *out_len = 0;
    }
    (void)out_data;
    (void)max_len;
    return true;
}

bool bt_gamepad_is_learn_ready(void)
{
    return s_learn_mode && !s_learn_pending && !s_learn_captured &&
           esp_timer_get_time() >= s_learn_next_capture_us;
}

esp_err_t bt_gamepad_skip_learn_key(void)
{
    if (!s_learn_mode || s_learn_key_index < 0 ||
        s_learn_key_index >= BT_GAMEPAD_NUM_KEYS) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_learn_captured) {
        bt_gamepad_store_current_candidate();
        ESP_LOGI(TAG, "保存 %s: %s byte=%u mask=0x%02X active=0x%02X",
                 bt_gamepad_key_names[s_learn_key_index],
                 s_learn_candidate.kind == BT_GAMEPAD_FIELD_HAT_VALUE ? "HAT" : "BIT",
                 (unsigned)s_learn_candidate.byte_index,
                 (unsigned)s_learn_candidate.mask,
                 (unsigned)s_learn_candidate.active_value);
    } else {
        memset(&s_keymap[s_learn_key_index], 0, sizeof(s_keymap[0]));
        ESP_LOGI(TAG, "跳过 %s", bt_gamepad_key_names[s_learn_key_index]);
    }

    s_learn_key_index++;
    s_learn_captured = false;
    s_learn_pending = false;
    s_learn_armed = false;
    s_learn_next_capture_us = esp_timer_get_time() +
                              (int64_t)BT_LEARN_EVENT_GUARD_MS * 1000;
    s_learn_pressed_len = 0;
    memset(s_learn_pressed_report, 0, sizeof(s_learn_pressed_report));
    memset(&s_learn_candidate, 0, sizeof(s_learn_candidate));
    s_learn_candidate_from_last = false;

    /* 保留上项松开报告，作为下一项 D1 的字段级参考。 */
    if (s_learn_key_index < BT_GAMEPAD_NUM_KEYS) {
        ESP_LOGI(TAG, "请松开所有按键；%d ms 后短按 %s，出现 D1 后松开",
                 BT_LEARN_EVENT_GUARD_MS,
                 bt_gamepad_key_names[s_learn_key_index]);
    } else {
        ESP_LOGI(TAG, "所有按键字段学习完成");
    }
    return ESP_OK;
}

esp_err_t bt_gamepad_finish_learn(void)
{
    if (!s_learn_mode) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_learn_key_index >= 0 && s_learn_key_index < BT_GAMEPAD_NUM_KEYS &&
        s_learn_captured) {
        bt_gamepad_store_current_candidate();
        s_learn_key_index++;
    }

    while (s_learn_key_index >= 0 && s_learn_key_index < BT_GAMEPAD_NUM_KEYS) {
        memset(&s_keymap[s_learn_key_index], 0, sizeof(s_keymap[0]));
        s_learn_key_index++;
    }

    /* 先用最后确认的全松开报告矫正可能被反向记录的按键极性。 */
    bt_gamepad_normalize_keymap_against_final_idle();

    s_learn_mode = false;
    s_learn_captured = false;
    s_learn_pending = false;
    s_learn_armed = false;
    s_keymap_loaded = true;
    s_joypad_state = 0xFF;

    return bt_gamepad_save_keymap();
}

/* ============================================================
 * 初始化与状态查询
 * ============================================================ */

esp_err_t bt_gamepad_start(void)
{
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        return ret;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        return ret;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_ble_gap_register_callback(bt_gamepad_gap_ble_cb);
    if (ret != ESP_OK) {
        return ret;
    }

    esp_hidh_config_t config = {
        .callback = bt_gamepad_hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ret = esp_hidh_init(&config);
    if (ret != ESP_OK) {
        return ret;
    }

    s_joypad_state = 0xFF;
    s_connected = false;
    s_connected_name[0] = '\0';
    s_connected_appearance = 0;

    ret = bt_gamepad_load_keymap();
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "尚无字段级映射，请执行一次按键学习");
    }

    ESP_LOGI(TAG, "蓝牙手柄字段级解析已启动 [%s]", BT_GAMEPAD_BUILD_ID);
    return ESP_OK;
}

bool bt_gamepad_is_connected(void)
{
    return s_connected;
}

const char *bt_gamepad_get_connected_name(void)
{
    return s_connected && s_connected_name[0] ? s_connected_name : NULL;
}

uint8_t bt_gamepad_get_joypad_state(void)
{
    return s_joypad_state;
}

/* ============================================================
 * 开机自动连接
 * ============================================================ */

esp_err_t bt_gamepad_try_auto_connect(void)
{
    /* 必须已有完整映射且设备指纹有效 */
    if (!s_keymap_loaded || !bt_gamepad_keymap_is_complete()) {
        ESP_LOGI(TAG, "自动连接：无完整映射，跳过");
        return ESP_FAIL;
    }

    bt_gamepad_device_fingerprint_t fingerprint = {0};
    if (!bt_gamepad_load_device_fingerprint(&fingerprint)) {
        ESP_LOGI(TAG, "自动连接：无设备指纹，跳过");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "自动连接：查找已配对手柄 %s (0x%04X)",
             fingerprint.name, fingerprint.appearance);

    /* 扫描 BLE 设备 */
    esp_err_t ret = bt_gamepad_start_scan();
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }

    /* 等待扫描完成（最多 8 秒） */
    for (int i = 0; i < 80 && bt_gamepad_is_scanning(); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* 在扫描结果中匹配名称 + appearance */
    bt_gamepad_scan_result_t results[BT_GAMEPAD_MAX_SCAN_RESULTS];
    int count = bt_gamepad_get_scan_results(results, BT_GAMEPAD_MAX_SCAN_RESULTS);

    int matched = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(results[i].name, fingerprint.name) == 0 &&
            results[i].appearance == fingerprint.appearance) {
            matched = i;
            break;
        }
    }

    if (matched < 0) {
        ESP_LOGI(TAG, "自动连接：未找到已配对手柄");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "自动连接：找到 %s，正在连接", results[matched].name);
    ret = bt_gamepad_connect_by_index(matched);
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }

    /* 等待连接建立（最多 10 秒） */
    for (int i = 0; i < 100; i++) {
        if (bt_gamepad_is_connected()) {
            ESP_LOGI(TAG, "自动连接成功：%s", s_connected_name);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(TAG, "自动连接超时");
    return ESP_FAIL;
}
