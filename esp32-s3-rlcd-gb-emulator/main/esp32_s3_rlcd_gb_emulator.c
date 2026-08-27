#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board_battery.h"
#include "board_clock.h"
#include "board_sdmmc.h"
#include "board_shtc3.h"
#include "board_speaker.h"
#include "board_rlcd.h"
#include "board_lvgl.h"
#include "audio_wave_ui.h"
#include "rlcd_test_pattern.h"
#include "gb_emu.h"
#include "gbc_emu.h"
#include "web_gamepad.h"
#include "bt_gamepad.h"
#include "wifi_time_sync.h"
#include "audio_player.h"

#define MAIN_SHOW_GB_EMU 1
#define MAIN_SHOW_RLCD_ORIENTATION_TEST 0
#define MAIN_SHOW_LVGL_RESPONSIVE_TEST 1
#define MAIN_LOAD_GB_ROM 1
#define MAIN_RUN_SD_SELF_TEST 0
#define MAIN_ENABLE_WEB_GAMEPAD MAIN_SHOW_GB_EMU
#define MAIN_ENABLE_BT_GAMEPAD MAIN_SHOW_GB_EMU
#define MAIN_ENABLE_ROTATION_KEY (!MAIN_SHOW_GB_EMU)
#define MAIN_GB_ROM_DIR "/sdcard"
#define MAIN_FONT_BIN_PATH "/sdcard/fonts/font16.bin"
#define MAIN_GB_ROM_PAGE_SIZE 8
#define MAIN_GB_ROM_PATH_MAX 256
#define MAIN_GB_ROM_NAME_MAX 128
#define MAIN_FONT_INDEX_ENTRY_SIZE 8
#define MAIN_FONT_MAX_BITMAP_BYTES 128
#define MAIN_KEY_GPIO GPIO_NUM_18
#define MAIN_BOOT_GPIO GPIO_NUM_0
#define MAIN_KEY_SCAN_INTERVAL_MS 20
#define MAIN_KEY_DEBOUNCE_COUNT 3
#define MAIN_MENU_LONG_PRESS_MS 700
#define MAIN_MENU_REPEAT_DELAY_MS 350
#define MAIN_MENU_REPEAT_INTERVAL_MS 120
#define MAIN_BT_CONFIG_TIMEOUT_MS 60000
#define MAIN_CLOCK_IDLE_MS 60000
#define MAIN_CLOCK_UPDATE_MS 1000
#define MAIN_CLOCK_ENV_UPDATE_MS 5000
#define MAIN_CLOCK_WIFI_SSID "321"
#define MAIN_CLOCK_WIFI_PASSWORD "3xe567z5"
#define MAIN_CLOCK_WIFI_SYNC_INITIAL_DELAY_MS 5000
#define MAIN_CLOCK_WIFI_SYNC_INTERVAL_MS (30 * 60 * 1000)
#define MAIN_CLOCK_WIFI_SYNC_TASK_STACK_SIZE (6 * 1024)
#define MAIN_CLOCK_WIFI_SYNC_TASK_PRIORITY 2
#define MAIN_KEY_TASK_STACK_SIZE (12 * 1024)
#define MAIN_GB_BUTTON_A (1U << 0)
#define MAIN_GB_BUTTON_B (1U << 1)
#define MAIN_GB_BUTTON_SELECT (1U << 2)
#define MAIN_GB_BUTTON_START (1U << 3)
#define MAIN_GB_BUTTON_RIGHT (1U << 4)
#define MAIN_GB_BUTTON_LEFT (1U << 5)
#define MAIN_GB_BUTTON_UP (1U << 6)
#define MAIN_GB_BUTTON_DOWN (1U << 7)

typedef enum {
    MAIN_MENU_ACTION_NONE = 0,
    MAIN_MENU_ACTION_UP,
    MAIN_MENU_ACTION_DOWN,
    MAIN_MENU_ACTION_PAGE_UP,
    MAIN_MENU_ACTION_PAGE_DOWN,
} main_menu_action_t;

static const char *TAG = "MAIN";
static gb_emu_rom_t s_gb_rom;
static bool s_gb_rom_loaded = false;

/*
 * 时钟屏保 WiFi 自动对时任务。
 * 在独立任务中运行，避免阻塞屏保的按键响应；
 * 完成后任务自删除，下次进入屏保再创建。
 */
static bool s_clock_sync_busy = false;
static bool s_clock_ever_synced = false;            /* 是否成功对过时 */
static int64_t s_clock_last_sync_us = 0;             /* 上次对时尝试完成时间 */

static void main_clock_wifi_sync_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "时钟屏保：开始 WiFi 自动对时");
    wifi_time_sync_result_t result = {0};
    esp_err_t ret = wifi_time_sync_once(MAIN_CLOCK_WIFI_SSID,
                                        MAIN_CLOCK_WIFI_PASSWORD,
                                        &result);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "时钟屏保：WiFi 对时成功 (elapsed=%lldms)",
                 (long long)result.elapsed_ms);
        s_clock_ever_synced = true;
    } else {
        ESP_LOGW(TAG, "时钟屏保：WiFi 对时失败：%s (elapsed=%lldms)",
                 esp_err_to_name(ret), (long long)result.elapsed_ms);
    }
    s_clock_last_sync_us = esp_timer_get_time();
    s_clock_sync_busy = false;
    vTaskDelete(NULL);
}

static void main_clock_wifi_sync_start(void)
{
    if (s_clock_sync_busy) {
        ESP_LOGI(TAG, "时钟屏保：WiFi 对时任务已在运行，跳过");
        return;
    }
    s_clock_sync_busy = true;
    BaseType_t ret = xTaskCreate(main_clock_wifi_sync_task,
                                 "main_wifi_sync",
                                 MAIN_CLOCK_WIFI_SYNC_TASK_STACK_SIZE,
                                 NULL,
                                 MAIN_CLOCK_WIFI_SYNC_TASK_PRIORITY,
                                 NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建 WiFi 对时任务失败");
        s_clock_sync_busy = false;
    }
}

/*
 * 获取合并后的手柄状态（web + bluetooth）。
 * 两者都是 8bit 低有效格式，取逻辑与：任一手柄按下即生效。
 */
static uint8_t main_get_merged_joypad(void)
{
#if MAIN_ENABLE_BT_GAMEPAD
    return web_gamepad_get_joypad_state() & bt_gamepad_get_joypad_state();
#else
    return web_gamepad_get_joypad_state();
#endif
}

typedef enum {
    MAIN_ROM_ENTRY_ROM = 0,
    MAIN_ROM_ENTRY_AUDIO,
    MAIN_ROM_ENTRY_DIR,
    MAIN_ROM_ENTRY_PARENT,
} main_rom_entry_type_t;

typedef struct {
    char path[MAIN_GB_ROM_PATH_MAX];
    char name[MAIN_GB_ROM_NAME_MAX];
    main_rom_entry_type_t type;
} main_gb_rom_entry_t;

static main_gb_rom_entry_t s_rom_entries[MAIN_GB_ROM_PAGE_SIZE];
static size_t s_rom_total_count = 0;
static size_t s_rom_page_start = 0;
static size_t s_rom_page_count = 0;
static char s_rom_current_dir[MAIN_GB_ROM_PATH_MAX] = MAIN_GB_ROM_DIR;

typedef struct {
    FILE *file;
    uint16_t width;
    uint16_t height;
    uint32_t glyph_count;
    uint32_t index_offset;
    uint32_t bitmap_bytes;
    uint32_t *codepoints;
    uint8_t *bitmaps;
    bool ready;
} main_font_bin_t;

static main_font_bin_t s_font_bin;
static board_shtc3_data_t s_clock_env_cache;
static int64_t s_clock_env_last_read_us = 0;
static bool s_clock_env_cache_valid = false;

static board_rlcd_rotation_t main_next_rotation(board_rlcd_rotation_t rotation)
{
    switch (rotation) {
    case BOARD_RLCD_ROTATION_0:
        return BOARD_RLCD_ROTATION_90;
    case BOARD_RLCD_ROTATION_90:
        return BOARD_RLCD_ROTATION_180;
    case BOARD_RLCD_ROTATION_180:
        return BOARD_RLCD_ROTATION_270;
    case BOARD_RLCD_ROTATION_270:
    default:
        return BOARD_RLCD_ROTATION_0;
    }
}

static const char *main_rotation_name(board_rlcd_rotation_t rotation)
{
    switch (rotation) {
    case BOARD_RLCD_ROTATION_0:
        return "0";
    case BOARD_RLCD_ROTATION_90:
        return "90";
    case BOARD_RLCD_ROTATION_180:
        return "180";
    case BOARD_RLCD_ROTATION_270:
        return "270";
    default:
        return "?";
    }
}

static esp_err_t main_apply_rotation(board_rlcd_rotation_t rotation)
{
#if MAIN_SHOW_RLCD_ORIENTATION_TEST
    ESP_RETURN_ON_ERROR(board_rlcd_set_rotation(rotation), TAG, "set RLCD rotation failed");
    return rlcd_test_pattern_draw_orientation();
#elif MAIN_SHOW_GB_EMU
    return board_rlcd_set_rotation(rotation);
#else
    return board_lvgl_set_rotation(rotation);
#endif
}

static void main_key_task(void *arg)
{
    (void)arg;

    bool stable_pressed = false;
    bool last_raw_pressed = false;
    uint8_t same_count = 0;

    while (1) {
        bool raw_pressed = gpio_get_level(MAIN_KEY_GPIO) == 0;

        if (raw_pressed == last_raw_pressed) {
            if (same_count < MAIN_KEY_DEBOUNCE_COUNT) {
                same_count++;
            }
        } else {
            same_count = 0;
            last_raw_pressed = raw_pressed;
        }

        if (same_count >= MAIN_KEY_DEBOUNCE_COUNT && raw_pressed != stable_pressed) {
            stable_pressed = raw_pressed;

            if (stable_pressed) {
                board_rlcd_rotation_t next_rotation = main_next_rotation(board_rlcd_get_rotation());
                esp_err_t ret = main_apply_rotation(next_rotation);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "按键触发屏幕旋转：%s 度", main_rotation_name(next_rotation));
                } else {
                    ESP_LOGE(TAG, "屏幕旋转失败：%s", esp_err_to_name(ret));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));
    }
}

static esp_err_t main_key_init(void)
{
#if !MAIN_ENABLE_ROTATION_KEY
    return ESP_OK;
#endif

    gpio_config_t key_config = {
        .pin_bit_mask = 1ULL << MAIN_KEY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&key_config), TAG, "config key gpio failed");

    BaseType_t task_ret = xTaskCreate(main_key_task, "main_key", MAIN_KEY_TASK_STACK_SIZE, NULL, 4, NULL);
    if (task_ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GPIO18 按键测试已启动：按一下旋转屏幕 90 度");
    return ESP_OK;
}

static bool main_has_gb_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }

    char ext[5] = {0};
    for (size_t i = 0; i < sizeof(ext) - 1 && dot[i] != '\0'; i++) {
        ext[i] = (char)tolower((unsigned char)dot[i]);
    }

    return strcmp(ext, ".gb") == 0 || strcmp(ext, ".gbc") == 0;
}

static bool main_has_audio_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }

    char ext[7] = {0};
    for (size_t i = 0; i < sizeof(ext) - 1 && dot[i] != '\0'; i++) {
        ext[i] = (char)tolower((unsigned char)dot[i]);
    }

    return strcmp(ext, ".wav") == 0 || strcmp(ext, ".mp3") == 0 || strcmp(ext, ".flac") == 0;
}

static bool main_has_rom_or_audio_extension(const char *name)
{
    return main_has_gb_extension(name) || main_has_audio_extension(name);
}

static bool main_is_audio_file_path(const char *path)
{
    const char *name = strrchr(path, '/');
    name = (name != NULL) ? name + 1 : path;
    return main_has_audio_extension(name);
}

static bool main_is_gbc_rom_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        return false;
    }

    char ext[5] = {0};
    for (size_t i = 0; i < sizeof(ext) - 1 && dot[i] != '\0'; i++) {
        ext[i] = (char)tolower((unsigned char)dot[i]);
    }

    return strcmp(ext, ".gbc") == 0;
}

static void main_copy_rom_name(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    size_t name_len = 0;
    while (name_len + 1 < dst_size && src[name_len] != '\0') {
        dst[name_len] = src[name_len];
        name_len++;
    }
    dst[name_len] = '\0';
}

static bool main_is_hidden_or_self_dir(const char *name)
{
    return name == NULL || name[0] == '\0' || name[0] == '.';
}

static esp_err_t main_join_path(char *dst, size_t dst_size, const char *dir_path, const char *name)
{
    int path_len = snprintf(dst, dst_size, "%s/%s", dir_path, name);
    if (path_len <= 0 || path_len >= (int)dst_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static bool main_is_directory_path(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }

    return S_ISDIR(st.st_mode);
}

static esp_err_t main_get_parent_dir(char *dst, size_t dst_size, const char *path)
{
    if (dst == NULL || dst_size == 0 || path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(path, MAIN_GB_ROM_DIR) == 0) {
        strlcpy(dst, MAIN_GB_ROM_DIR, dst_size);
        return ESP_OK;
    }

    strlcpy(dst, path, dst_size);
    char *slash = strrchr(dst, '/');
    if (slash == NULL || slash <= dst + strlen(MAIN_GB_ROM_DIR)) {
        strlcpy(dst, MAIN_GB_ROM_DIR, dst_size);
        return ESP_OK;
    }

    *slash = '\0';
    return ESP_OK;
}

static bool main_should_show_dir_entry(const char *dir_path, const char *name, char *path, size_t path_size)
{
    if (main_is_hidden_or_self_dir(name)) {
        return false;
    }

    if (main_join_path(path, path_size, dir_path, name) != ESP_OK) {
        ESP_LOGW(TAG, "目录项路径过长，跳过：%s", name);
        return false;
    }

    return main_is_directory_path(path);
}

static bool main_should_show_rom_entry(const char *dir_path, const char *name, char *path, size_t path_size)
{
    if (main_is_hidden_or_self_dir(name)) {
        return false;
    }

    if (!main_has_rom_or_audio_extension(name)) {
        return false;
    }

    if (main_join_path(path, path_size, dir_path, name) != ESP_OK) {
        ESP_LOGW(TAG, "ROM 路径过长，跳过：%s", name);
        return false;
    }

    return !main_is_directory_path(path);
}

static void main_set_entry_type(main_gb_rom_entry_t *entry)
{
    if (main_has_audio_extension(entry->name)) {
        entry->type = MAIN_ROM_ENTRY_AUDIO;
    } else {
        entry->type = MAIN_ROM_ENTRY_ROM;
    }
}

static esp_err_t main_add_page_entry(size_t *visible_index,
                                     size_t page_start,
                                     const char *path,
                                     const char *name,
                                     main_rom_entry_type_t type)
{
    if (*visible_index < page_start) {
        (*visible_index)++;
        return ESP_OK;
    }

    if (s_rom_page_count >= MAIN_GB_ROM_PAGE_SIZE) {
        return ESP_ERR_NO_MEM;
    }

    strlcpy(s_rom_entries[s_rom_page_count].path, path, sizeof(s_rom_entries[s_rom_page_count].path));
    main_copy_rom_name(s_rom_entries[s_rom_page_count].name,
                       sizeof(s_rom_entries[s_rom_page_count].name),
                       name);
    if (type == MAIN_ROM_ENTRY_ROM) {
        main_set_entry_type(&s_rom_entries[s_rom_page_count]);
    } else {
        s_rom_entries[s_rom_page_count].type = type;
    }
    s_rom_page_count++;
    (*visible_index)++;
    return ESP_OK;
}

static esp_err_t main_load_rom_browser_page(const char *dir_path, size_t page_start)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "无法打开目录：%s", dir_path);
        return ESP_FAIL;
    }

    s_rom_page_start = page_start;
    s_rom_page_count = 0;
    size_t visible_index = 0;

    if (strcmp(dir_path, MAIN_GB_ROM_DIR) != 0) {
        char parent_path[MAIN_GB_ROM_PATH_MAX];
        ESP_RETURN_ON_ERROR(main_get_parent_dir(parent_path, sizeof(parent_path), dir_path),
                            TAG,
                            "get parent dir failed");
        esp_err_t ret = main_add_page_entry(&visible_index, page_start, parent_path, "..", MAIN_ROM_ENTRY_PARENT);
        if (ret == ESP_ERR_NO_MEM) {
            closedir(dir);
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(ret, TAG, "add parent entry failed");
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        char path[MAIN_GB_ROM_PATH_MAX];
        if (!main_should_show_dir_entry(dir_path, entry->d_name, path, sizeof(path))) {
            continue;
        }

        esp_err_t ret = main_add_page_entry(&visible_index, page_start, path, entry->d_name, MAIN_ROM_ENTRY_DIR);
        if (ret == ESP_ERR_NO_MEM) {
            break;
        }
        ESP_RETURN_ON_ERROR(ret, TAG, "add dir entry failed");
    }

    rewinddir(dir);

    while ((entry = readdir(dir)) != NULL) {
        char path[MAIN_GB_ROM_PATH_MAX];
        if (!main_should_show_rom_entry(dir_path, entry->d_name, path, sizeof(path))) {
            continue;
        }

        esp_err_t ret = main_add_page_entry(&visible_index, page_start, path, entry->d_name, MAIN_ROM_ENTRY_ROM);
        if (ret == ESP_ERR_NO_MEM) {
            break;
        }
        ESP_RETURN_ON_ERROR(ret, TAG, "add rom entry failed");
    }

    closedir(dir);
    return ESP_OK;
}

static esp_err_t main_scan_rom_browser(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "无法打开目录：%s", dir_path);
        return ESP_FAIL;
    }

    s_rom_total_count = 0;
    s_rom_page_start = 0;
    s_rom_page_count = 0;
    memset(s_rom_entries, 0, sizeof(s_rom_entries));
    strlcpy(s_rom_current_dir, dir_path, sizeof(s_rom_current_dir));

    if (strcmp(dir_path, MAIN_GB_ROM_DIR) != 0) {
        s_rom_total_count++;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        char path[MAIN_GB_ROM_PATH_MAX];
        if (main_should_show_dir_entry(dir_path, entry->d_name, path, sizeof(path))) {
            s_rom_total_count++;
        }
    }

    rewinddir(dir);

    while ((entry = readdir(dir)) != NULL) {
        char path[MAIN_GB_ROM_PATH_MAX];
        if (main_should_show_rom_entry(dir_path, entry->d_name, path, sizeof(path))) {
            s_rom_total_count++;
        }
    }

    closedir(dir);

    ESP_LOGI(TAG, "目录扫描完成：%s，共 %u 项", dir_path, (unsigned)s_rom_total_count);
    if (s_rom_total_count == 0) {
        return ESP_OK;
    }

    return main_load_rom_browser_page(dir_path, 0);
}

static esp_err_t main_ensure_rom_browser_page(size_t selected)
{
    size_t page_start = (selected / MAIN_GB_ROM_PAGE_SIZE) * MAIN_GB_ROM_PAGE_SIZE;
    if (selected >= s_rom_page_start && selected < s_rom_page_start + s_rom_page_count) {
        return ESP_OK;
    }

    return main_load_rom_browser_page(s_rom_current_dir, page_start);
}

static uint16_t main_read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t main_read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

/* Embedded font binary (linked into flash via EMBED_FILES) */
extern const uint8_t font16_bin_start[] asm("_binary_font16_bin_start");
extern const uint8_t font16_bin_end[]   asm("_binary_font16_bin_end");

static esp_err_t main_font_bin_init_embedded(void)
{
    if (s_font_bin.ready) {
        return ESP_OK;
    }

    const uint8_t *data = font16_bin_start;
    size_t data_len = (size_t)(font16_bin_end - font16_bin_start);

    if (data_len < 12) {
        ESP_LOGE(TAG, "内嵌中文字库太小：%u bytes", (unsigned)data_len);
        return ESP_FAIL;
    }

    if (memcmp(data, "FNT1", 4) != 0) {
        ESP_LOGE(TAG, "内嵌中文字库格式错误");
        return ESP_ERR_INVALID_VERSION;
    }

    uint16_t width = main_read_le16(&data[4]);
    uint16_t height = main_read_le16(&data[6]);
    uint32_t glyph_count = main_read_le32(&data[8]);
    uint32_t bytes_per_row = (width + 7U) / 8U;
    uint32_t bitmap_bytes = bytes_per_row * height;

    if (width == 0 || height == 0 || glyph_count == 0 || bitmap_bytes > MAIN_FONT_MAX_BITMAP_BYTES) {
        ESP_LOGE(TAG, "内嵌中文字库参数不支持：%ux%u glyphs=%u bitmap=%u",
                 (unsigned)width, (unsigned)height,
                 (unsigned)glyph_count, (unsigned)bitmap_bytes);
        return ESP_ERR_INVALID_ARG;
    }

    /* Verify embedded data is large enough */
    size_t index_size = (size_t)glyph_count * MAIN_FONT_INDEX_ENTRY_SIZE;
    size_t bitmap_size = (size_t)glyph_count * bitmap_bytes;
    if (data_len < 12 + index_size + bitmap_size) {
        ESP_LOGE(TAG, "内嵌中文字库数据不完整：%u < %u",
                 (unsigned)data_len, (unsigned)(12 + index_size + bitmap_size));
        return ESP_FAIL;
    }

    /*
     * bitmaps stay in flash (direct pointer, no copy needed).
     * codepoints are extracted to PSRAM for binary search
     * (flash is not suitable for uint32_t array random access).
     */
    size_t codepoint_bytes = glyph_count * sizeof(uint32_t);
    uint32_t *codepoints = (uint32_t *)heap_caps_malloc(codepoint_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (codepoints == NULL) {
        ESP_LOGE(TAG, "中文字库索引申请失败：%u bytes", (unsigned)codepoint_bytes);
        return ESP_ERR_NO_MEM;
    }

    const uint8_t *index_data = data + 12;
    for (uint32_t i = 0; i < glyph_count; i++) {
        codepoints[i] = main_read_le32(&index_data[i * MAIN_FONT_INDEX_ENTRY_SIZE]);
    }

    const uint8_t *bitmaps = index_data + index_size;  /* points directly into flash */

    s_font_bin.file = NULL;
    s_font_bin.width = width;
    s_font_bin.height = height;
    s_font_bin.glyph_count = glyph_count;
    s_font_bin.index_offset = 12;
    s_font_bin.bitmap_bytes = bitmap_bytes;
    s_font_bin.codepoints = codepoints;
    s_font_bin.bitmaps = (uint8_t *)bitmaps;
    s_font_bin.ready = true;

    ESP_LOGI(TAG,
             "中文字库已从 Flash 加载：%ux%u glyphs=%u (bitmaps 在 flash, index %u bytes 在 PSRAM)",
             (unsigned)width, (unsigned)height,
             (unsigned)glyph_count, (unsigned)codepoint_bytes);
    return ESP_OK;
}

static const uint8_t *main_font_bin_lookup(uint32_t codepoint)
{
    if (!s_font_bin.ready || s_font_bin.codepoints == NULL || s_font_bin.bitmaps == NULL) {
        return NULL;
    }

    uint32_t left = 0;
    uint32_t right = s_font_bin.glyph_count;

    while (left < right) {
        uint32_t mid = left + (right - left) / 2;
        uint32_t entry_codepoint = s_font_bin.codepoints[mid];
        if (entry_codepoint == codepoint) {
            return &s_font_bin.bitmaps[mid * s_font_bin.bitmap_bytes];
        }

        if (entry_codepoint < codepoint) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return NULL;
}

static esp_err_t main_draw_hline(uint16_t x, uint16_t y, uint16_t w, board_rlcd_color_t color)
{
    for (uint16_t i = 0; i < w; i++) {
        ESP_RETURN_ON_ERROR(board_rlcd_set_pixel((uint16_t)(x + i), y, color), TAG, "set pixel failed");
    }

    return ESP_OK;
}

static esp_err_t main_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, board_rlcd_color_t color)
{
    for (uint16_t row = 0; row < h; row++) {
        ESP_RETURN_ON_ERROR(main_draw_hline(x, (uint16_t)(y + row), w, color), TAG, "draw hline failed");
    }

    return ESP_OK;
}

static esp_err_t main_draw_folder_icon(uint16_t x, uint16_t y, board_rlcd_color_t color)
{
    /*
     * 14x11 的 1bit 文件夹图标。
     * 用像素图案而不是字体符号，保证不同字库下形状稳定。
     */
    static const uint16_t rows[] = {
        0b00000000000000,
        0b00111110000000,
        0b01111111110000,
        0b11111111111100,
        0b11111111111110,
        0b11000000000110,
        0b11000000000110,
        0b11000000000110,
        0b11000000000110,
        0b11111111111110,
        0b11111111111110,
    };

    for (uint16_t row = 0; row < 11; row++) {
        for (uint16_t col = 0; col < 14; col++) {
            if ((rows[row] & (1U << (13U - col))) != 0) {
                ESP_RETURN_ON_ERROR(board_rlcd_set_pixel((uint16_t)(x + col), (uint16_t)(y + row), color),
                                    TAG,
                                    "draw folder icon failed");
            }
        }
    }

    return ESP_OK;
}

static const uint8_t *main_get_3x5_glyph(char ch)
{
    static const uint8_t glyph_space[5] = {0, 0, 0, 0, 0};
    static const uint8_t glyph_unknown[5] = {7, 1, 2, 0, 2};
    static const uint8_t glyph_0[5] = {7, 5, 5, 5, 7};
    static const uint8_t glyph_1[5] = {2, 6, 2, 2, 7};
    static const uint8_t glyph_2[5] = {7, 1, 7, 4, 7};
    static const uint8_t glyph_3[5] = {7, 1, 7, 1, 7};
    static const uint8_t glyph_4[5] = {5, 5, 7, 1, 1};
    static const uint8_t glyph_5[5] = {7, 4, 7, 1, 7};
    static const uint8_t glyph_6[5] = {7, 4, 7, 5, 7};
    static const uint8_t glyph_7[5] = {7, 1, 1, 1, 1};
    static const uint8_t glyph_8[5] = {7, 5, 7, 5, 7};
    static const uint8_t glyph_9[5] = {7, 5, 7, 1, 7};
    static const uint8_t glyph_a[5] = {7, 5, 7, 5, 5};
    static const uint8_t glyph_b[5] = {6, 5, 6, 5, 6};
    static const uint8_t glyph_c[5] = {7, 4, 4, 4, 7};
    static const uint8_t glyph_d[5] = {6, 5, 5, 5, 6};
    static const uint8_t glyph_e[5] = {7, 4, 6, 4, 7};
    static const uint8_t glyph_f[5] = {7, 4, 6, 4, 4};
    static const uint8_t glyph_g[5] = {7, 4, 5, 5, 7};
    static const uint8_t glyph_h[5] = {5, 5, 7, 5, 5};
    static const uint8_t glyph_i[5] = {7, 2, 2, 2, 7};
    static const uint8_t glyph_j[5] = {1, 1, 1, 5, 7};
    static const uint8_t glyph_k[5] = {5, 5, 6, 5, 5};
    static const uint8_t glyph_l[5] = {4, 4, 4, 4, 7};
    static const uint8_t glyph_m[5] = {5, 7, 7, 5, 5};
    static const uint8_t glyph_n[5] = {6, 5, 5, 5, 5};
    static const uint8_t glyph_o[5] = {7, 5, 5, 5, 7};
    static const uint8_t glyph_p[5] = {7, 5, 7, 4, 4};
    static const uint8_t glyph_q[5] = {7, 5, 5, 7, 1};
    static const uint8_t glyph_r[5] = {7, 5, 6, 5, 5};
    static const uint8_t glyph_s[5] = {7, 4, 7, 1, 7};
    static const uint8_t glyph_t[5] = {7, 2, 2, 2, 2};
    static const uint8_t glyph_u[5] = {5, 5, 5, 5, 7};
    static const uint8_t glyph_v[5] = {5, 5, 5, 5, 2};
    static const uint8_t glyph_w[5] = {5, 5, 7, 7, 5};
    static const uint8_t glyph_x[5] = {5, 5, 2, 5, 5};
    static const uint8_t glyph_y[5] = {5, 5, 2, 2, 2};
    static const uint8_t glyph_z[5] = {7, 1, 2, 4, 7};
    static const uint8_t glyph_dot[5] = {0, 0, 0, 0, 2};
    static const uint8_t glyph_dash[5] = {0, 0, 7, 0, 0};
    static const uint8_t glyph_under[5] = {0, 0, 0, 0, 7};
    static const uint8_t glyph_slash[5] = {1, 1, 2, 4, 4};
    static const uint8_t glyph_colon[5] = {0, 2, 0, 2, 0};
    static const uint8_t glyph_percent[5] = {5, 1, 2, 4, 5};
    static const uint8_t glyph_gt[5] = {4, 2, 1, 2, 4};

    ch = (char)toupper((unsigned char)ch);
    switch (ch) {
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case 'A': return glyph_a;
    case 'B': return glyph_b;
    case 'C': return glyph_c;
    case 'D': return glyph_d;
    case 'E': return glyph_e;
    case 'F': return glyph_f;
    case 'G': return glyph_g;
    case 'H': return glyph_h;
    case 'I': return glyph_i;
    case 'J': return glyph_j;
    case 'K': return glyph_k;
    case 'L': return glyph_l;
    case 'M': return glyph_m;
    case 'N': return glyph_n;
    case 'O': return glyph_o;
    case 'P': return glyph_p;
    case 'Q': return glyph_q;
    case 'R': return glyph_r;
    case 'S': return glyph_s;
    case 'T': return glyph_t;
    case 'U': return glyph_u;
    case 'V': return glyph_v;
    case 'W': return glyph_w;
    case 'X': return glyph_x;
    case 'Y': return glyph_y;
    case 'Z': return glyph_z;
    case '.': return glyph_dot;
    case '-': return glyph_dash;
    case '_': return glyph_under;
    case '/': return glyph_slash;
    case ':': return glyph_colon;
    case '%': return glyph_percent;
    case '>': return glyph_gt;
    case ' ': return glyph_space;
    default: return glyph_unknown;
    }
}

static esp_err_t main_draw_char_3x5(uint16_t x,
                                    uint16_t y,
                                    char ch,
                                    uint8_t scale,
                                    board_rlcd_color_t color)
{
    const uint8_t *glyph = main_get_3x5_glyph(ch);

    for (uint8_t row = 0; row < 5; row++) {
        for (uint8_t col = 0; col < 3; col++) {
            if ((glyph[row] & (1U << (2 - col))) != 0) {
                ESP_RETURN_ON_ERROR(main_fill_rect((uint16_t)(x + col * scale),
                                                   (uint16_t)(y + row * scale),
                                                   scale,
                                                   scale,
                                                   color),
                                    TAG,
                                    "draw char pixel failed");
            }
        }
    }

    return ESP_OK;
}

static uint16_t main_text_3x5_scaled_width(const char *text, uint8_t scale)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }

    size_t len = strlen(text);
    return (uint16_t)(len * 3U * scale + (len - 1U) * scale);
}

static esp_err_t main_draw_text_3x5_scaled(uint16_t x,
                                           uint16_t y,
                                           const char *text,
                                           uint8_t scale,
                                           board_rlcd_color_t color)
{
    uint16_t cursor_x = x;

    while (text != NULL && *text != '\0') {
        ESP_RETURN_ON_ERROR(main_draw_char_3x5(cursor_x, y, *text, scale, color),
                            TAG,
                            "draw scaled text char failed");
        cursor_x = (uint16_t)(cursor_x + 4U * scale);
        text++;
    }

    return ESP_OK;
}

static esp_err_t main_draw_author_logo(uint16_t width)
{
    const uint16_t tile = 15;
    const uint16_t gap = 2;
    const uint16_t logo_w = tile * 3 + gap * 2;
    const uint16_t x = width > logo_w + 10 ? (uint16_t)(width - logo_w - 10) : 0;
    const uint16_t y = 11;

#define DRAW_AUTHOR_RECT(px, py, pw, ph, color) \
    ESP_RETURN_ON_ERROR(main_fill_rect((uint16_t)(px), (uint16_t)(py), (pw), (ph), (color)), \
                        TAG, \
                        "draw author logo failed")

    for (uint8_t i = 0; i < 3; i++) {
        uint16_t tx = (uint16_t)(x + i * (tile + gap));
        DRAW_AUTHOR_RECT(tx, y, tile, tile, BOARD_RLCD_COLOR_WHITE);
    }

    /* 按像素稿绘制的 1bit 版 15A 图标。 */
    uint16_t tx0 = x;
    DRAW_AUTHOR_RECT((uint16_t)(tx0 + 3), (uint16_t)(y + 3), 3, 9, BOARD_RLCD_COLOR_BLACK);
    DRAW_AUTHOR_RECT((uint16_t)(tx0 + 9), (uint16_t)(y + 3), 3, 9, BOARD_RLCD_COLOR_BLACK);

    uint16_t tx1 = (uint16_t)(x + tile + gap);
    DRAW_AUTHOR_RECT((uint16_t)(tx1 + 6), (uint16_t)(y + 3), 6, 6, BOARD_RLCD_COLOR_BLACK);
    DRAW_AUTHOR_RECT((uint16_t)(tx1 + 3), (uint16_t)(y + 6), 6, 6, BOARD_RLCD_COLOR_BLACK);
    DRAW_AUTHOR_RECT((uint16_t)(tx1 + 6), (uint16_t)(y + 6), 3, 3, BOARD_RLCD_COLOR_WHITE);

    uint16_t tx2 = (uint16_t)(x + (tile + gap) * 2);
    DRAW_AUTHOR_RECT((uint16_t)(tx2 + 3), (uint16_t)(y + 3), 9, 3, BOARD_RLCD_COLOR_BLACK);
    DRAW_AUTHOR_RECT((uint16_t)(tx2 + 3), (uint16_t)(y + 9), 9, 3, BOARD_RLCD_COLOR_BLACK);

#undef DRAW_AUTHOR_RECT

    return ESP_OK;
}

static bool main_utf8_next(const char **text, uint32_t *codepoint)
{
    const uint8_t *p = (const uint8_t *)*text;
    if (p == NULL || *p == '\0' || codepoint == NULL) {
        return false;
    }

    if (p[0] < 0x80) {
        *codepoint = p[0];
        *text += 1;
        return true;
    }

    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *codepoint = ((uint32_t)(p[0] & 0x1F) << 6) |
                     (uint32_t)(p[1] & 0x3F);
        *text += 2;
        return true;
    }

    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *codepoint = ((uint32_t)(p[0] & 0x0F) << 12) |
                     ((uint32_t)(p[1] & 0x3F) << 6) |
                     (uint32_t)(p[2] & 0x3F);
        *text += 3;
        return true;
    }

    if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *codepoint = ((uint32_t)(p[0] & 0x07) << 18) |
                     ((uint32_t)(p[1] & 0x3F) << 12) |
                     ((uint32_t)(p[2] & 0x3F) << 6) |
                     (uint32_t)(p[3] & 0x3F);
        *text += 4;
        return true;
    }

    *codepoint = '?';
    *text += 1;
    return true;
}

static esp_err_t main_draw_font_bin_glyph(uint16_t x,
                                          uint16_t y,
                                          uint32_t codepoint,
                                          board_rlcd_color_t color)
{
    const uint8_t *bitmap = main_font_bin_lookup(codepoint);

    if (bitmap == NULL) {
        return main_draw_char_3x5(x, (uint16_t)(y + 3), '?', 2, color);
    }

    uint32_t bytes_per_row = (s_font_bin.width + 7U) / 8U;
    for (uint16_t row = 0; row < s_font_bin.height; row++) {
        for (uint16_t col = 0; col < s_font_bin.width; col++) {
            uint8_t byte = bitmap[row * bytes_per_row + col / 8U];
            uint8_t bit = (uint8_t)(7U - (col & 7U));
            if ((byte & (1U << bit)) != 0) {
                ESP_RETURN_ON_ERROR(board_rlcd_set_pixel((uint16_t)(x + col),
                                                         (uint16_t)(y + row),
                                                         color),
                                    TAG,
                                    "draw font glyph pixel failed");
            }
        }
    }

    return ESP_OK;
}

static esp_err_t main_draw_text_utf8(uint16_t x,
                                     uint16_t y,
                                     const char *text,
                                     uint16_t max_width,
                                     board_rlcd_color_t color)
{
    uint16_t cursor_x = x;
    uint16_t end_x = (uint16_t)(x + max_width);

    while (text != NULL && *text != '\0' && cursor_x < end_x) {
        const char *before = text;
        uint32_t codepoint = 0;
        if (!main_utf8_next(&text, &codepoint)) {
            break;
        }

        if (s_font_bin.ready) {
            uint16_t char_w = s_font_bin.width;
            if (cursor_x + char_w > end_x) {
                text = before;
                break;
            }
            ESP_RETURN_ON_ERROR(main_draw_font_bin_glyph(cursor_x, y, codepoint, color),
                                TAG,
                                "draw font glyph failed");
            cursor_x = (uint16_t)(cursor_x + char_w);
            continue;
        }

        if (codepoint < 0x80) {
            uint16_t char_w = 8;
            if (cursor_x + char_w > end_x) {
                break;
            }
            ESP_RETURN_ON_ERROR(main_draw_char_3x5(cursor_x, (uint16_t)(y + 3), (char)codepoint, 2, color),
                                TAG,
                                "draw ascii failed");
            cursor_x = (uint16_t)(cursor_x + char_w);
            continue;
        }

        uint16_t char_w = 8;
        if (cursor_x + char_w > end_x) {
            text = before;
            break;
        }

        ESP_RETURN_ON_ERROR(main_draw_char_3x5(cursor_x, (uint16_t)(y + 3), '?', 2, color),
                            TAG,
                            "draw missing non-ascii glyph failed");
        cursor_x = (uint16_t)(cursor_x + char_w);
    }

    return ESP_OK;
}

static esp_err_t main_draw_rom_menu(size_t selected)
{
    const uint16_t width = board_rlcd_get_logical_width();
    const uint16_t height = board_rlcd_get_logical_height();
    const uint16_t row_h = 28;
    const uint16_t list_y = 48;
    uint16_t visible_rows = (uint16_t)((height - list_y - 20) / row_h);
    if (visible_rows > MAIN_GB_ROM_PAGE_SIZE) {
        visible_rows = MAIN_GB_ROM_PAGE_SIZE;
    }

    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_WHITE), TAG, "clear menu failed");
    ESP_RETURN_ON_ERROR(main_fill_rect(0, 0, width, 34, BOARD_RLCD_COLOR_BLACK), TAG, "draw title bg failed");
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(14, 11, "SD ROM BROWSER", (uint16_t)(width - 110), BOARD_RLCD_COLOR_WHITE),
                        TAG,
                        "draw title failed");

    if (s_rom_total_count == 0) {
        ESP_RETURN_ON_ERROR(main_draw_text_utf8(28, 96, "EMPTY FOLDER", (uint16_t)(width - 56), BOARD_RLCD_COLOR_BLACK),
                            TAG,
                            "draw empty failed");
        ESP_RETURN_ON_ERROR(main_draw_text_utf8(28, 144, "COPY .GB/.GBC/.WAV/.MP3", (uint16_t)(width - 56), BOARD_RLCD_COLOR_BLACK),
                            TAG,
                            "draw empty help failed");
        return board_rlcd_flush();
    }

    ESP_RETURN_ON_ERROR(main_ensure_rom_browser_page(selected), TAG, "load rom page failed");

    char count_text[32];
    snprintf(count_text, sizeof(count_text), "%u/%u", (unsigned)(selected + 1), (unsigned)s_rom_total_count);
    ESP_RETURN_ON_ERROR(main_draw_text_utf8((uint16_t)(width - 96), 11, count_text, 94, BOARD_RLCD_COLOR_WHITE),
                        TAG,
                        "draw count failed");

    for (uint16_t row = 0; row < visible_rows; row++) {
        if (row >= s_rom_page_count) {
            break;
        }

        size_t rom_index = s_rom_page_start + row;
        uint16_t y = (uint16_t)(list_y + row * row_h);
        bool is_selected = rom_index == selected;
        board_rlcd_color_t bg = is_selected ? BOARD_RLCD_COLOR_BLACK : BOARD_RLCD_COLOR_WHITE;
        board_rlcd_color_t fg = is_selected ? BOARD_RLCD_COLOR_WHITE : BOARD_RLCD_COLOR_BLACK;

        ESP_RETURN_ON_ERROR(main_fill_rect(8, y, (uint16_t)(width - 16), (uint16_t)(row_h - 4), bg),
                            TAG,
                            "draw row bg failed");
        ESP_RETURN_ON_ERROR(main_draw_text_utf8(16,
                                                (uint16_t)(y + 6),
                                                is_selected ? ">" : " ",
                                                16,
                                                fg),
                            TAG,
                            "draw cursor failed");
        if (s_rom_entries[row].type == MAIN_ROM_ENTRY_DIR) {
            ESP_RETURN_ON_ERROR(main_draw_folder_icon(32, (uint16_t)(y + 5), fg),
                                TAG,
                                "draw folder icon failed");
        } else if (s_rom_entries[row].type == MAIN_ROM_ENTRY_PARENT) {
            ESP_RETURN_ON_ERROR(main_draw_text_utf8(34,
                                                    (uint16_t)(y + 6),
                                                    "U",
                                                    16,
                                                    fg),
                                TAG,
                                "draw parent marker failed");
        } else if (s_rom_entries[row].type == MAIN_ROM_ENTRY_AUDIO) {
            ESP_RETURN_ON_ERROR(main_draw_text_utf8(34,
                                                    (uint16_t)(y + 6),
                                                    "~",
                                                    16,
                                                    fg),
                                TAG,
                                "draw audio marker failed");
        }
        ESP_RETURN_ON_ERROR(main_draw_text_utf8(58,
                                                (uint16_t)(y + 4),
                                                s_rom_entries[row].name,
                                                (uint16_t)(width - 72),
                                                fg),
                            TAG,
                            "draw rom name failed");
    }

    ESP_RETURN_ON_ERROR(main_draw_text_utf8(12,
                                            (uint16_t)(height - 20),
                                            "BOOT:OPEN  KEY-HOLD:BT  ~:AUDIO",
                                            (uint16_t)(width - 24),
                                            BOARD_RLCD_COLOR_BLACK),
                        TAG,
                        "draw help failed");
    return board_rlcd_flush();
}

static esp_err_t main_draw_clock_screen(void)
{
    const uint16_t width = board_rlcd_get_logical_width();
    const uint16_t height = board_rlcd_get_logical_height();

    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_BLACK), TAG, "clear clock failed");

    board_clock_status_t clock_status = board_clock_get_status();
    if (!clock_status.synced) {
        const char *title = "WAIT PHONE TIME";
        const char *hint = "OPEN GAMEPAD PAGE";
        uint16_t title_w = main_text_3x5_scaled_width(title, 5);
        uint16_t hint_w = main_text_3x5_scaled_width(hint, 3);

        ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - title_w) / 2),
                                                      (uint16_t)(height / 2 - 42),
                                                      title,
                                                      5,
                                                      BOARD_RLCD_COLOR_WHITE),
                            TAG,
                            "draw wait time title failed");
        ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - hint_w) / 2),
                                                      (uint16_t)(height / 2 + 18),
                                                      hint,
                                                      3,
                                                      BOARD_RLCD_COLOR_WHITE),
                            TAG,
                            "draw wait time hint failed");
        ESP_RETURN_ON_ERROR(main_draw_author_logo(width), TAG, "draw author logo failed");
        return board_rlcd_flush();
    }

    time_t local_seconds = board_clock_get_phone_local_seconds();
    struct tm local_tm = {0};
    gmtime_r(&local_seconds, &local_tm);

    char time_text[8];
    char date_text[40];
    snprintf(time_text,
             sizeof(time_text),
             local_tm.tm_sec % 2 == 0 ? "%02d:%02d" : "%02d %02d",
             local_tm.tm_hour,
             local_tm.tm_min);
    snprintf(date_text,
             sizeof(date_text),
             "%04d-%02d-%02d",
             local_tm.tm_year + 1900,
             local_tm.tm_mon + 1,
             local_tm.tm_mday);

    uint8_t time_scale = width >= 360 ? 14 : 10;
    uint16_t time_w = main_text_3x5_scaled_width(time_text, time_scale);
    uint16_t date_w = main_text_3x5_scaled_width(date_text, 4);

    ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - time_w) / 2),
                                                  (uint16_t)(height / 2 - 58),
                                                  time_text,
                                                  time_scale,
                                                  BOARD_RLCD_COLOR_WHITE),
                        TAG,
                        "draw clock time failed");
    ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - date_w) / 2),
                                                  (uint16_t)(height / 2 + 24),
                                                  date_text,
                                                  4,
                                                  BOARD_RLCD_COLOR_WHITE),
                        TAG,
                        "draw clock date failed");

    board_battery_status_t battery = {0};
    char battery_text[20];
    if (board_battery_read(&battery) == ESP_OK) {
        snprintf(battery_text, sizeof(battery_text), "BAT %u%%", (unsigned)battery.percent);
    } else {
        snprintf(battery_text, sizeof(battery_text), "BAT --%%");
    }

    int64_t now_us = esp_timer_get_time();
    if (!s_clock_env_cache_valid ||
        now_us - s_clock_env_last_read_us >= MAIN_CLOCK_ENV_UPDATE_MS * 1000LL) {
        board_shtc3_data_t env = {0};
        if (board_shtc3_read(&env) == ESP_OK) {
            s_clock_env_cache = env;
            s_clock_env_cache_valid = true;
        }
        s_clock_env_last_read_us = now_us;
    }

    char env_text[32];
    if (s_clock_env_cache_valid) {
        snprintf(env_text,
                 sizeof(env_text),
                 "TEMP %.1fC RH %.0f%%",
                 (double)s_clock_env_cache.temperature_c,
                 (double)s_clock_env_cache.humidity_percent);
    } else {
        snprintf(env_text, sizeof(env_text), "TEMP --.-C RH --%%");
    }

    ESP_RETURN_ON_ERROR(main_draw_text_utf8(12,
                                            12,
                                            "ESP32 GB CLOCK",
                                            (uint16_t)(width - 24),
                                            BOARD_RLCD_COLOR_WHITE),
                        TAG,
                        "draw clock header failed");
    ESP_RETURN_ON_ERROR(main_draw_author_logo(width), TAG, "draw author logo failed");
    uint16_t env_w = main_text_3x5_scaled_width(env_text, 3);
    ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - env_w) / 2),
                                                  (uint16_t)(height / 2 + 62),
                                                  env_text,
                                                  3,
                                                  BOARD_RLCD_COLOR_WHITE),
                        TAG,
                        "draw clock env failed");
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(12,
                                            (uint16_t)(height - 28),
                                            battery_text,
                                            120,
                                            BOARD_RLCD_COLOR_WHITE),
                        TAG,
                        "draw clock battery failed");
    ESP_RETURN_ON_ERROR(main_draw_text_utf8((uint16_t)(width - 156),
                                            (uint16_t)(height - 28),
                                            "KEY/PHONE:BACK",
                                            144,
                                            BOARD_RLCD_COLOR_WHITE),
                        TAG,
                        "draw clock help failed");

    return board_rlcd_flush();
}

static esp_err_t main_key_config_input(void)
{
    gpio_config_t key_config = {
        .pin_bit_mask = (1ULL << MAIN_KEY_GPIO) | (1ULL << MAIN_BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&key_config);
}

/* ============================================================
 * 蓝牙手柄配置页面（扫描 + 按键学习）
 * ============================================================
 *
 * 启动后在 ROM 菜单前显示，流程：
 *   1. 扫描 BLE HID 设备，显示列表
 *   2. 用板载按键操作：KEY(GPIO18) 短按=下移切换，BOOT(GPIO0) 短按=确认
 *      列表末尾追加 [RESCAN] 和 [SKIP] 虚拟条目
 *   3. 连接成功后进入按键学习模式
 *   4. 依次提示 10 个按键，用户在蓝牙手柄上按对应键
 *   5. 学习阶段 GPIO18：短按=跳过当前键，长按=完成学习
 *   6. 全部完成后进入 ROM 菜单
 */

#define MAIN_BT_SCAN_PAGE_SIZE 8

static esp_err_t main_draw_bt_scan_page(int selected, int scan_count,
                                        const bt_gamepad_scan_result_t *results)
{
    const uint16_t width = board_rlcd_get_logical_width();
    const uint16_t height = board_rlcd_get_logical_height();
    const uint16_t row_h = 28;
    const uint16_t list_y = 48;
    uint16_t visible_rows = (uint16_t)((height - list_y - 20) / row_h);
    if (visible_rows > MAIN_BT_SCAN_PAGE_SIZE) {
        visible_rows = MAIN_BT_SCAN_PAGE_SIZE;
    }
    int total = scan_count + 2; /* 设备 + [RESCAN] + [SKIP] */

    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_WHITE), TAG, "clear bt scan failed");
    ESP_RETURN_ON_ERROR(main_fill_rect(0, 0, width, 34, BOARD_RLCD_COLOR_BLACK), TAG, "draw bt title bg failed");

    const char *title = bt_gamepad_is_scanning() ? "BT SCAN..." : "BT SCAN DONE";
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(14, 11, title, (uint16_t)(width - 120), BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw bt title failed");

    char count_text[32];
    snprintf(count_text, sizeof(count_text), "%d FOUND", scan_count);
    ESP_RETURN_ON_ERROR(main_draw_text_utf8((uint16_t)(width - 110), 11, count_text, 100, BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw bt count failed");

    /* 滚动窗口 */
    int start = 0;
    if (total > (int)visible_rows) {
        if (selected >= (int)visible_rows) {
            start = selected - (int)visible_rows + 1;
            if (start > total - (int)visible_rows) {
                start = total - (int)visible_rows;
            }
        }
    }

    int draw_rows = total < (int)visible_rows ? total : (int)visible_rows;
    for (int row = 0; row < draw_rows; row++) {
        int idx = start + row;
        uint16_t y = (uint16_t)(list_y + row * row_h);
        bool is_selected = idx == selected;
        board_rlcd_color_t bg = is_selected ? BOARD_RLCD_COLOR_BLACK : BOARD_RLCD_COLOR_WHITE;
        board_rlcd_color_t fg = is_selected ? BOARD_RLCD_COLOR_WHITE : BOARD_RLCD_COLOR_BLACK;

        ESP_RETURN_ON_ERROR(main_fill_rect(8, y, (uint16_t)(width - 16), (uint16_t)(row_h - 4), bg),
                            TAG, "draw bt row bg failed");
        ESP_RETURN_ON_ERROR(main_draw_text_utf8(16, (uint16_t)(y + 6),
                                                 is_selected ? ">" : " ", 16, fg),
                            TAG, "draw bt cursor failed");

        char label[40];
        if (idx < scan_count) {
            snprintf(label, sizeof(label), "%s", results[idx].name);
        } else if (idx == scan_count) {
            snprintf(label, sizeof(label), "[RESCAN]");
        } else {
            snprintf(label, sizeof(label), "[SKIP]");
        }
        ESP_RETURN_ON_ERROR(main_draw_text_utf8(34, (uint16_t)(y + 4), label,
                                                 (uint16_t)(width - 50), fg),
                            TAG, "draw bt label failed");
    }

    ESP_RETURN_ON_ERROR(main_draw_text_utf8(12, (uint16_t)(height - 20),
                                             "KEY:NEXT  BOOT:SELECT",
                                             (uint16_t)(width - 24), BOARD_RLCD_COLOR_BLACK),
                        TAG, "draw bt help failed");
    return board_rlcd_flush();
}

static esp_err_t main_draw_bt_learn_page(int key_index, bool captured, bool ready)
{
    const uint16_t width = board_rlcd_get_logical_width();
    const uint16_t height = board_rlcd_get_logical_height();

    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_WHITE), TAG, "clear bt learn failed");
    ESP_RETURN_ON_ERROR(main_fill_rect(0, 0, width, 34, BOARD_RLCD_COLOR_BLACK), TAG, "draw learn title bg failed");

    /* 标题 */
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(14, 11, "BT KEY LEARN",
                                             (uint16_t)(width - 60), BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw learn title failed");

    /* 进度 */
    char progress[20];
    snprintf(progress, sizeof(progress), "%d/%d", key_index + 1, BT_GAMEPAD_NUM_KEYS);
    ESP_RETURN_ON_ERROR(main_draw_text_utf8((uint16_t)(width - 56), 11, progress, 50, BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw learn progress failed");

    /*
     * 十键采用两列五行布局：左列 A/B/X/Y/SELECT，右列 START 与方向键。
     * 保持每行的可读字号，避免十行列表侵占底部 PRESS 提示区域。
     */
    const uint16_t list_y = 48;
    const uint16_t row_h = 24;
    const int rows_per_column = (BT_GAMEPAD_NUM_KEYS + 1) / 2;
    const uint16_t column_width = (uint16_t)((width - 24) / 2);
    for (int i = 0; i < BT_GAMEPAD_NUM_KEYS; i++) {
        int column = i / rows_per_column;
        int row = i % rows_per_column;
        uint16_t x = (uint16_t)(12 + column * column_width);
        uint16_t y = (uint16_t)(list_y + row * row_h);
        const char *status;
        board_rlcd_color_t color;
        if (i < key_index) {
            status = "OK";
            /* 普通行是白底，使用黑字。 */
            color = BOARD_RLCD_COLOR_BLACK;
        } else if (i == key_index) {
            if (captured) {
                status = "GOT!";
            } else if (ready) {
                status = "PRESS";
            } else {
                status = "WAIT";
            }
            /* 当前行先填充黑底，必须用白字。 */
            color = BOARD_RLCD_COLOR_WHITE;
        } else {
            status = "--";
            /* 未学习普通行同样是白底黑字。 */
            color = BOARD_RLCD_COLOR_BLACK;
        }

        if (i == key_index) {
            ESP_RETURN_ON_ERROR(main_fill_rect((uint16_t)(x - 4), y,
                                                (uint16_t)(column_width - 4),
                                                (uint16_t)(row_h - 4),
                                                BOARD_RLCD_COLOR_BLACK),
                                TAG, "draw learn row bg failed");
        }
        ESP_RETURN_ON_ERROR(main_draw_text_utf8(x, (uint16_t)(y + 4),
                                                 bt_gamepad_key_names[i],
                                                 (uint16_t)(column_width - 60), color),
                            TAG, "draw learn key name failed");
        ESP_RETURN_ON_ERROR(main_draw_text_utf8((uint16_t)(x + column_width - 54),
                                                 (uint16_t)(y + 4), status, 50, color),
                            TAG, "draw learn status failed");
    }

    /* 底部提示 */
    if (key_index < BT_GAMEPAD_NUM_KEYS) {
        const char *hint;
        if (captured) {
            hint = "RELEASE KEY";
        } else if (ready) {
            char hint_buf[64];
            snprintf(hint_buf, sizeof(hint_buf), "PRESS [%s] ON PAD", bt_gamepad_key_names[key_index]);
            uint16_t hint_w = main_text_3x5_scaled_width(hint_buf, 3);
            ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - hint_w) / 2),
                                                           (uint16_t)(height - 36),
                                                           hint_buf, 3, BOARD_RLCD_COLOR_BLACK),
                                TAG, "draw learn hint failed");
            hint = NULL;
        } else {
            hint = "INITIALIZING...";
        }
        if (hint) {
            uint16_t hint_w = main_text_3x5_scaled_width(hint, 3);
            ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - hint_w) / 2),
                                                           (uint16_t)(height - 36),
                                                           hint, 3, BOARD_RLCD_COLOR_BLACK),
                                TAG, "draw learn hint failed");
        }
    }

    return board_rlcd_flush();
}

/* 已有兼容映射时的 BLE 手柄启动选择页。 */
static esp_err_t main_draw_bt_saved_keymap_page(void)
{
    const uint16_t width = board_rlcd_get_logical_width();
    const uint16_t height = board_rlcd_get_logical_height();
    const char *title = "BT MAP FOUND";
    const char *line1 = "SELECT: PLAY";
    const char *line2 = "START: RELEARN";

    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_WHITE), TAG,
                        "clear saved map page failed");
    ESP_RETURN_ON_ERROR(main_fill_rect(0, 0, width, 34, BOARD_RLCD_COLOR_BLACK), TAG,
                        "draw saved map title bg failed");
    ESP_RETURN_ON_ERROR(main_draw_text_utf8((uint16_t)((width - 96) / 2), 11,
                                             title, 96, BOARD_RLCD_COLOR_WHITE), TAG,
                        "draw saved map title failed");

    uint16_t line1_w = main_text_3x5_scaled_width(line1, 4);
    uint16_t line2_w = main_text_3x5_scaled_width(line2, 4);
    ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - line1_w) / 2),
                                                   (uint16_t)(height / 2 - 28),
                                                   line1, 4, BOARD_RLCD_COLOR_BLACK), TAG,
                        "draw saved map select failed");
    ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - line2_w) / 2),
                                                   (uint16_t)(height / 2 + 12),
                                                   line2, 4, BOARD_RLCD_COLOR_BLACK), TAG,
                        "draw saved map start failed");
    return board_rlcd_flush();
}

/* 返回 true 表示 Select 复用已保存映射；false 表示 Start 重新学习。 */
static esp_err_t main_wait_bt_saved_keymap_choice(bool *reuse_saved_keymap)
{
    if (reuse_saved_keymap == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *reuse_saved_keymap = false;

    ESP_RETURN_ON_ERROR(main_draw_bt_saved_keymap_page(), TAG,
                        "draw saved keymap choice failed");
    ESP_LOGI(TAG, "发现兼容映射：按 SELECT 跳过学习，按 START 重新学习");

    /* 忽略进入页面前可能遗留的状态，必须等待新的按下边沿。 */
    bt_gamepad_reset_joypad_state();
    uint8_t previous = 0xFF;
    while (true) {
        uint8_t current = bt_gamepad_get_joypad_state();
        uint8_t pressed_edges = (uint8_t)(previous & (uint8_t)~current);
        previous = current;

        if ((pressed_edges & MAIN_GB_BUTTON_SELECT) != 0) {
            *reuse_saved_keymap = true;
            bt_gamepad_reset_joypad_state();
            ESP_LOGI(TAG, "SELECT：复用已保存蓝牙手柄映射");
            return ESP_OK;
        }
        if ((pressed_edges & MAIN_GB_BUTTON_START) != 0) {
            bt_gamepad_reset_joypad_state();
            ESP_LOGI(TAG, "START：请求重新学习蓝牙手柄按键");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));
    }
}

static esp_err_t main_wait_bt_config(void)
{
#if MAIN_ENABLE_BT_GAMEPAD
    ESP_RETURN_ON_ERROR(main_key_config_input(), TAG, "config bt key failed");

    const uint16_t width = board_rlcd_get_logical_width();
    const uint16_t height = board_rlcd_get_logical_height();

    /* ---- 阶段 1: BLE 扫描 ---- */
    ESP_RETURN_ON_ERROR(bt_gamepad_start_scan(), TAG, "start bt scan failed");

    int selected = 0;
    bool prev_scanning = true;
    int prev_scan_count = -1;

    /* GPIO18(KEY) 与 GPIO0(BOOT) 去抖状态 */
    bool key_stable_pressed = false;
    bool key_last_raw_pressed = false;
    uint8_t key_same_count = 0;
    bool boot_stable_pressed = false;
    bool boot_last_raw_pressed = false;
    uint8_t boot_same_count = 0;

    int64_t config_start_us = esp_timer_get_time();

    while (1) {
        bool key_raw_pressed = gpio_get_level(MAIN_KEY_GPIO) == 0;
        bool boot_raw_pressed = gpio_get_level(MAIN_BOOT_GPIO) == 0;
        bool scanning = bt_gamepad_is_scanning();

        bt_gamepad_scan_result_t results[BT_GAMEPAD_MAX_SCAN_RESULTS];
        int scan_count = bt_gamepad_get_scan_results(results, BT_GAMEPAD_MAX_SCAN_RESULTS);
        int total = scan_count + 2;

        if (selected >= total) {
            selected = total - 1;
        }

        /* 重绘页面（扫描状态或结果数变化时） */
        if (scanning != prev_scanning || scan_count != prev_scan_count) {
            ESP_RETURN_ON_ERROR(main_draw_bt_scan_page(selected, scan_count, results),
                                TAG, "draw bt scan page failed");
            prev_scanning = scanning;
            prev_scan_count = scan_count;
        }

        /* KEY(GPIO18) 去抖：短按 = 下移切换 */
        if (key_raw_pressed == key_last_raw_pressed) {
            if (key_same_count < MAIN_KEY_DEBOUNCE_COUNT) {
                key_same_count++;
            }
        } else {
            key_same_count = 0;
            key_last_raw_pressed = key_raw_pressed;
        }

        if (key_same_count >= MAIN_KEY_DEBOUNCE_COUNT && key_raw_pressed != key_stable_pressed) {
            key_stable_pressed = key_raw_pressed;
            if (!key_stable_pressed) {
                /* KEY 短按 = 切换下移 */
                if (!scanning) {
                    selected = (selected + 1) % total;
                    ESP_RETURN_ON_ERROR(main_draw_bt_scan_page(selected, scan_count, results),
                                        TAG, "redraw bt scan failed");
                }
            }
        }

        /* BOOT(GPIO0) 去抖：短按 = 确认 */
        if (boot_raw_pressed == boot_last_raw_pressed) {
            if (boot_same_count < MAIN_KEY_DEBOUNCE_COUNT) {
                boot_same_count++;
            }
        } else {
            boot_same_count = 0;
            boot_last_raw_pressed = boot_raw_pressed;
        }

        if (boot_same_count >= MAIN_KEY_DEBOUNCE_COUNT && boot_raw_pressed != boot_stable_pressed) {
            boot_stable_pressed = boot_raw_pressed;
            if (!boot_stable_pressed) {
                /* BOOT 短按 = 确认/选择 */
                if (!scanning) {
                    if (selected < scan_count) {
                        /* 连接设备 */
                        ESP_LOGI(TAG, "选择连接 BLE 设备 [%d]: %s", selected, results[selected].name);
                        esp_err_t conn_ret = bt_gamepad_connect_by_index(selected);
                        if (conn_ret != ESP_OK) {
                            ESP_LOGW(TAG, "连接失败: %s", esp_err_to_name(conn_ret));
                        }
                        for (int wait = 0; wait < 50; wait++) {
                            if (bt_gamepad_is_connected()) {
                                break;
                            }
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                        if (bt_gamepad_is_connected()) {
                            ESP_LOGI(TAG, "BLE 手柄已连接，进入按键学习");
                            break;
                        } else {
                            ESP_LOGW(TAG, "连接超时，重新扫描");
                            bt_gamepad_start_scan();
                            prev_scanning = true;
                            selected = 0;
                            vTaskDelay(pdMS_TO_TICKS(500));
                            continue;
                        }
                    } else if (selected == scan_count) {
                        /* [RESCAN] */
                        ESP_LOGI(TAG, "重新扫描");
                        bt_gamepad_start_scan();
                        prev_scanning = true;
                        selected = 0;
                        vTaskDelay(pdMS_TO_TICKS(500));
                        continue;
                    } else {
                        /* [SKIP] */
                        ESP_LOGI(TAG, "跳过蓝牙配置");
                        return ESP_OK;
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));

        /* 超时自动跳过蓝牙配置 */
        if (esp_timer_get_time() - config_start_us >= MAIN_BT_CONFIG_TIMEOUT_MS * 1000LL) {
            ESP_LOGI(TAG, "蓝牙配置 %d 秒无操作，自动跳过", MAIN_BT_CONFIG_TIMEOUT_MS / 1000);
            return ESP_OK;
        }
    }

    /* ---- 阶段 2: 已保存映射选择 / 按键学习 ---- */
    if (bt_gamepad_has_compatible_saved_keymap()) {
        bool reuse_saved_keymap = false;
        ESP_RETURN_ON_ERROR(main_wait_bt_saved_keymap_choice(&reuse_saved_keymap), TAG,
                            "wait saved keymap choice failed");
        if (reuse_saved_keymap) {
            /* 保持已加载映射，直接返回 ROM 浏览器/游戏入口。 */
            return ESP_OK;
        }
    }

    ESP_RETURN_ON_ERROR(bt_gamepad_start_learn(), TAG, "start bt learn failed");
    ESP_RETURN_ON_ERROR(main_draw_bt_learn_page(0, false, false), TAG, "draw learn page failed");

    /* 重置 GPIO18(KEY)/GPIO0(BOOT) 去抖状态 */
    key_stable_pressed = false;
    key_last_raw_pressed = false;
    key_same_count = 0;
    boot_stable_pressed = false;
    boot_last_raw_pressed = false;
    boot_same_count = 0;

    int prev_learn_key = 0;
    bool prev_captured = false;
    bool prev_ready = false;

    while (1) {
        int learn_key = bt_gamepad_get_learn_key();

        /* 全部完成 */
        if (learn_key < 0) {
            break;
        }

        bool captured = bt_gamepad_check_learned(NULL, 0, NULL);
        bool ready = bt_gamepad_is_learn_ready();

        /* 重绘（状态变化时） */
        if (learn_key != prev_learn_key || captured != prev_captured || ready != prev_ready) {
            ESP_RETURN_ON_ERROR(main_draw_bt_learn_page(learn_key, captured, ready),
                                TAG, "redraw learn page failed");
            prev_learn_key = learn_key;
            prev_captured = captured;
            prev_ready = ready;
        }

        /* GPIO18 跳过/完成功能暂时注释，每次重启强制学习全部按键 */
        #if 0
        /* GPIO18 去抖 */
        if (raw_pressed == last_raw_pressed) {
            if (same_count < MAIN_KEY_DEBOUNCE_COUNT) {
                same_count++;
            }
        } else {
            same_count = 0;
            last_raw_pressed = raw_pressed;
        }

        if (same_count >= MAIN_KEY_DEBOUNCE_COUNT && raw_pressed != stable_pressed) {
            stable_pressed = raw_pressed;
            if (stable_pressed) {
                press_start_us = esp_timer_get_time();
            } else {
                int64_t press_ms = (esp_timer_get_time() - press_start_us) / 1000;
                if (press_ms >= MAIN_MENU_LONG_PRESS_MS) {
                    /* 长按 = 完成学习 */
                    ESP_LOGI(TAG, "完成按键学习");
                    break;
                } else {
                    /* 短按 = 跳过当前按键 */
                    ESP_RETURN_ON_ERROR(bt_gamepad_skip_learn_key(), TAG, "skip learn key failed");
                    vTaskDelay(pdMS_TO_TICKS(300));
                    continue;
                }
            }
        }
        #endif

        /* 检查是否已捕获按键（自动前进） */
        if (captured) {
            ESP_RETURN_ON_ERROR(bt_gamepad_skip_learn_key(), TAG, "advance learn key failed");
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));
    }

    /* 完成学习，保存 */
    ESP_RETURN_ON_ERROR(bt_gamepad_finish_learn(), TAG, "finish learn failed");

    /* 显示完成提示 */
    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_WHITE), TAG, "clear done failed");
    const char *done = "BT CONFIG DONE";
    uint16_t done_w = main_text_3x5_scaled_width(done, 4);
    ESP_RETURN_ON_ERROR(main_draw_text_3x5_scaled((uint16_t)((width - done_w) / 2),
                                                   (uint16_t)(height / 2 - 10),
                                                   done, 4, BOARD_RLCD_COLOR_BLACK),
                        TAG, "draw done failed");
    board_rlcd_flush();
    vTaskDelay(pdMS_TO_TICKS(1500));

#endif /* MAIN_ENABLE_BT_GAMEPAD */
    return ESP_OK;
}

static size_t main_move_rom_selection(size_t current, main_menu_action_t action)
{
    if (s_rom_total_count == 0) {
        return current;
    }

    switch (action) {
    case MAIN_MENU_ACTION_UP:
        return (current + s_rom_total_count - 1) % s_rom_total_count;
    case MAIN_MENU_ACTION_DOWN:
        return (current + 1) % s_rom_total_count;
    case MAIN_MENU_ACTION_PAGE_UP: {
        size_t row = current % MAIN_GB_ROM_PAGE_SIZE;
        size_t current_page_start = (current / MAIN_GB_ROM_PAGE_SIZE) * MAIN_GB_ROM_PAGE_SIZE;
        size_t last_page_start = ((s_rom_total_count - 1) / MAIN_GB_ROM_PAGE_SIZE) * MAIN_GB_ROM_PAGE_SIZE;
        size_t prev_page_start = current_page_start == 0 ? last_page_start : current_page_start - MAIN_GB_ROM_PAGE_SIZE;
        size_t next = prev_page_start + row;
        return next >= s_rom_total_count ? s_rom_total_count - 1 : next;
    }
    case MAIN_MENU_ACTION_PAGE_DOWN: {
        size_t row = current % MAIN_GB_ROM_PAGE_SIZE;
        size_t current_page_start = (current / MAIN_GB_ROM_PAGE_SIZE) * MAIN_GB_ROM_PAGE_SIZE;
        size_t next_page_start = current_page_start + MAIN_GB_ROM_PAGE_SIZE;
        if (next_page_start >= s_rom_total_count) {
            next_page_start = 0;
        }
        size_t next = next_page_start + row;
        return next >= s_rom_total_count ? s_rom_total_count - 1 : next;
    }
    case MAIN_MENU_ACTION_NONE:
    default:
        return current;
    }
}

static esp_err_t main_apply_rom_menu_action(size_t *selected, main_menu_action_t action)
{
    size_t next = main_move_rom_selection(*selected, action);
    if (next == *selected) {
        return ESP_OK;
    }

    *selected = next;
    return main_draw_rom_menu(*selected);
}

static esp_err_t main_activate_rom_browser_entry(size_t *selected, bool *rom_selected, bool *audio_selected)
{
    if (selected == NULL || rom_selected == NULL || audio_selected == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *rom_selected = false;
    *audio_selected = false;
    ESP_RETURN_ON_ERROR(main_ensure_rom_browser_page(*selected), TAG, "load selected entry page failed");

    main_gb_rom_entry_t *entry = &s_rom_entries[*selected - s_rom_page_start];
    if (entry->type == MAIN_ROM_ENTRY_ROM) {
        ESP_LOGI(TAG, "选择 ROM：%s", entry->path);
        *rom_selected = true;
        return ESP_OK;
    }
    if (entry->type == MAIN_ROM_ENTRY_AUDIO) {
        ESP_LOGI(TAG, "选择音频：%s", entry->path);
        *audio_selected = true;
        return ESP_OK;
    }

    char target_path[MAIN_GB_ROM_PATH_MAX];
    strlcpy(target_path, entry->path, sizeof(target_path));

    ESP_LOGI(TAG, "进入目录：%s", target_path);
    ESP_RETURN_ON_ERROR(main_scan_rom_browser(target_path), TAG, "scan selected dir failed");
    *selected = 0;
    return main_draw_rom_menu(*selected);
}

static esp_err_t main_return_rom_browser_parent(size_t *selected)
{
    if (selected == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(s_rom_current_dir, MAIN_GB_ROM_DIR) == 0) {
        return ESP_OK;
    }

    char parent_path[MAIN_GB_ROM_PATH_MAX];
    ESP_RETURN_ON_ERROR(main_get_parent_dir(parent_path, sizeof(parent_path), s_rom_current_dir),
                        TAG,
                        "get current parent dir failed");

    ESP_LOGI(TAG, "返回上级目录：%s", parent_path);
    ESP_RETURN_ON_ERROR(main_scan_rom_browser(parent_path), TAG, "scan parent dir failed");
    *selected = 0;
    return main_draw_rom_menu(*selected);
}

/* ── 音频播放界面 ──────────────────────────────────────────────────── */

static esp_err_t main_draw_audio_play_screen(const char *filename, audio_player_state_t state,
                                             size_t track_index, size_t track_total)
{
    const uint16_t width = board_rlcd_get_logical_width();
    const uint16_t height = board_rlcd_get_logical_height();

    ESP_RETURN_ON_ERROR(board_rlcd_clear(BOARD_RLCD_COLOR_BLACK), TAG, "clear audio screen failed");
    ESP_RETURN_ON_ERROR(main_fill_rect(0, 0, width, 34, BOARD_RLCD_COLOR_BLACK), TAG, "draw title bg failed");

    const char *title = "AUDIO PLAYER";
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(14, 11, title, (uint16_t)(width - 24), BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw title failed");

    /* Track index */
    if (track_total > 1) {
        char track_text[32];
        snprintf(track_text, sizeof(track_text), "TRACK %u/%u",
                 (unsigned)(track_index + 1), (unsigned)track_total);
        ESP_RETURN_ON_ERROR(main_draw_text_utf8(14, 34, track_text, (uint16_t)(width - 28), BOARD_RLCD_COLOR_WHITE),
                            TAG, "draw track text failed");
    }

    /* File name */
    char display_name[MAIN_GB_ROM_NAME_MAX];
    main_copy_rom_name(display_name, sizeof(display_name), filename);
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(14, 60, display_name, (uint16_t)(width - 28), BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw filename failed");

    /* Format and sample rate */
    char info_text[64];
    const char *fmt_str = (audio_player_get_format() == AUDIO_PLAYER_FORMAT_WAV) ? "WAV" : "MP3";
    uint32_t sr = audio_player_get_sample_rate();
    if (sr > 0) {
        snprintf(info_text, sizeof(info_text), "%s %uHz", fmt_str, (unsigned)sr);
    } else {
        snprintf(info_text, sizeof(info_text), "%s", fmt_str);
    }
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(14, 85, info_text, (uint16_t)(width - 28), BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw info failed");

    /* State indicator */
    const char *state_str = "";
    switch (state) {
        case AUDIO_PLAYER_STATE_PLAYING:  state_str = "> PLAYING";  break;
        case AUDIO_PLAYER_STATE_PAUSED:   state_str = "|| PAUSED";  break;
        case AUDIO_PLAYER_STATE_STOPPED:  state_str = "[] STOPPED"; break;
        case AUDIO_PLAYER_STATE_ERROR:    state_str = "! ERROR";    break;
        default:                           state_str = "...";        break;
    }
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(14, 110, state_str, (uint16_t)(width - 28), BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw state failed");

    /* Volume */
    char vol_text[32];
    snprintf(vol_text, sizeof(vol_text), "VOL %u%%", audio_player_get_volume());
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(14, 135, vol_text, (uint16_t)(width - 28), BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw vol failed");

    /* Bottom hint (two lines for all controls) */
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(12, (uint16_t)(height - 36),
                                             "START:EXIT A:PREV B:NEXT",
                                             (uint16_t)(width - 24), BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw hint1 failed");
    ESP_RETURN_ON_ERROR(main_draw_text_utf8(12, (uint16_t)(height - 18),
                                             "UP:+ DOWN:-",
                                             (uint16_t)(width - 24), BOARD_RLCD_COLOR_WHITE),
                        TAG, "draw hint2 failed");
    return board_rlcd_flush();
}

/* Maximum audio files in a directory for sequential playback */
#define MAIN_AUDIO_MAX_TRACKS 128

static esp_err_t main_wait_audio_playback(const char *start_path)
{
    /* Extract directory from start_path */
    char dir_path[MAIN_GB_ROM_PATH_MAX];
    strlcpy(dir_path, start_path, sizeof(dir_path));
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash != NULL && last_slash != dir_path) {
        *last_slash = '\0';
    } else {
        strlcpy(dir_path, "/", sizeof(dir_path));
    }

    /* Scan directory and collect all audio file paths */
    static char track_paths[MAIN_AUDIO_MAX_TRACKS][MAIN_GB_ROM_PATH_MAX];
    size_t track_count = 0;
    int start_track = -1;

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "音频目录无法打开：%s", dir_path);
        return ESP_FAIL;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && track_count < MAIN_AUDIO_MAX_TRACKS) {
        if (main_is_hidden_or_self_dir(ent->d_name)) {
            continue;
        }
        if (!main_has_audio_extension(ent->d_name)) {
            continue;
        }
        char full_path[MAIN_GB_ROM_PATH_MAX];
        if (main_join_path(full_path, sizeof(full_path), dir_path, ent->d_name) != ESP_OK) {
            continue;
        }
        if (main_is_directory_path(full_path)) {
            continue;
        }

        strlcpy(track_paths[track_count], full_path, sizeof(track_paths[track_count]));
        if (strcmp(full_path, start_path) == 0) {
            start_track = (int)track_count;
        }
        track_count++;
    }
    closedir(dir);

    if (track_count == 0) {
        ESP_LOGE(TAG, "目录中无音频文件：%s", dir_path);
        return ESP_FAIL;
    }
    if (start_track < 0) {
        start_track = 0;
    }

    ESP_LOGI(TAG, "音频播放：目录 %s，共 %u 首，从第 %u 首开始",
             dir_path, (unsigned)track_count, (unsigned)(start_track + 1));

    /* Sequential playback loop */
    size_t current_track = (size_t)start_track;
    bool key_debounce_stable = false;
    bool key_last_raw = false;
    uint8_t key_same = 0;
    bool boot_stable = false;
    bool boot_last_raw = false;
    uint8_t boot_same = 0;
    uint8_t last_pad = main_get_merged_joypad();
    uint8_t last_vol = audio_player_get_volume();
    int vol_repeat_ms = 0;

    while (current_track < track_count) {
        const char *path = track_paths[current_track];
        const char *filename = strrchr(path, '/');
        filename = (filename != NULL) ? filename + 1 : path;

        ESP_LOGI(TAG, "播放第 %u/%u 首：%s", (unsigned)(current_track + 1), (unsigned)track_count, path);
        ESP_RETURN_ON_ERROR(audio_player_play(path), TAG, "audio play failed");

        audio_player_state_t last_drawn = (audio_player_state_t)-1;
        bool track_done = false;
        bool skip_to_next = false;
        bool skip_to_prev = false;

        while (!track_done) {
            audio_player_state_t st = audio_player_get_state();

            /* Redraw if state or volume changed */
            uint8_t cur_vol = audio_player_get_volume();
            if (st != last_drawn || cur_vol != last_vol) {
                ESP_RETURN_ON_ERROR(main_draw_audio_play_screen(filename, st, current_track, track_count),
                                    TAG, "draw audio screen failed");
                last_drawn = st;
                last_vol = cur_vol;
            }

            /* Playback finished naturally or error → advance to next track */
            if (st == AUDIO_PLAYER_STATE_IDLE || st == AUDIO_PLAYER_STATE_ERROR) {
                if (last_drawn != (audio_player_state_t)-1 &&
                    last_drawn != AUDIO_PLAYER_STATE_STARTING) {
                    vTaskDelay(pdMS_TO_TICKS(300));
                    if (st == AUDIO_PLAYER_STATE_ERROR) {
                        ESP_LOGW(TAG, "第 %u 首播放出错，跳过", (unsigned)(current_track + 1));
                    }
                    track_done = true;
                }
                /* Still STARTING, wait */
            }

            /* KEY(GPIO18) short press = exit entire audio playback */
            bool key_raw = gpio_get_level(MAIN_KEY_GPIO) == 0;
            if (key_raw == key_last_raw) {
                if (key_same < MAIN_KEY_DEBOUNCE_COUNT) key_same++;
            } else {
                key_same = 0;
                key_last_raw = key_raw;
            }
            if (key_same >= MAIN_KEY_DEBOUNCE_COUNT && key_raw != key_debounce_stable) {
                key_debounce_stable = key_raw;
                if (!key_debounce_stable) {
                    ESP_LOGI(TAG, "KEY 短按，退出音频播放");
                    audio_player_stop();
                    return ESP_OK;
                }
            }

            /* BOOT(GPIO0) short press = skip to next track */
            bool boot_raw = gpio_get_level(MAIN_BOOT_GPIO) == 0;
            if (boot_raw == boot_last_raw) {
                if (boot_same < MAIN_KEY_DEBOUNCE_COUNT) boot_same++;
            } else {
                boot_same = 0;
                boot_last_raw = boot_raw;
            }
            if (boot_same >= MAIN_KEY_DEBOUNCE_COUNT && boot_raw != boot_stable) {
                boot_stable = boot_raw;
                if (!boot_stable) {
                    ESP_LOGI(TAG, "BOOT 短按，跳到下一首");
                    audio_player_stop();
                    skip_to_next = true;
                    track_done = true;
                }
            }

            /* Gamepad UP/DOWN = volume control (edge-triggered + hold repeat) */
            uint8_t pad = main_get_merged_joypad();
            bool up_pressed = (pad & MAIN_GB_BUTTON_UP) == 0;
            bool down_pressed = (pad & MAIN_GB_BUTTON_DOWN) == 0;
            bool up_edge = up_pressed && (last_pad & MAIN_GB_BUTTON_UP) != 0;
            bool down_edge = down_pressed && (last_pad & MAIN_GB_BUTTON_DOWN) != 0;

            if (up_edge || down_edge) {
                uint8_t vol = audio_player_get_volume();
                if (up_edge && vol < 100) {
                    vol = (vol >= 95) ? 100 : vol + 5;
                    audio_player_set_volume(vol);
                    ESP_LOGI(TAG, "音量+ → %u%%", vol);
                }
                if (down_edge && vol > 0) {
                    vol = (vol <= 5) ? 0 : vol - 5;
                    audio_player_set_volume(vol);
                    ESP_LOGI(TAG, "音量- → %u%%", vol);
                }
                vol_repeat_ms = 0;
            } else if (up_pressed || down_pressed) {
                /* Hold repeat after 400ms delay, every 200ms */
                vol_repeat_ms += MAIN_KEY_SCAN_INTERVAL_MS;
                if (vol_repeat_ms >= 400) {
                    uint8_t vol = audio_player_get_volume();
                    if (up_pressed && vol < 100) {
                        vol = (vol >= 95) ? 100 : vol + 5;
                        audio_player_set_volume(vol);
                    }
                    if (down_pressed && vol > 0) {
                        vol = (vol <= 5) ? 0 : vol - 5;
                        audio_player_set_volume(vol);
                    }
                    vol_repeat_ms = 200;  /* repeat interval */
                }
            } else {
                vol_repeat_ms = 0;
            }
            /* Gamepad START = exit (same as KEY), A = previous track, B = next track (same as BOOT) */
            bool start_edge = (pad & MAIN_GB_BUTTON_START) == 0 && (last_pad & MAIN_GB_BUTTON_START) != 0;
            bool a_edge = (pad & MAIN_GB_BUTTON_A) == 0 && (last_pad & MAIN_GB_BUTTON_A) != 0;
            bool b_edge = (pad & MAIN_GB_BUTTON_B) == 0 && (last_pad & MAIN_GB_BUTTON_B) != 0;

            if (start_edge) {
                ESP_LOGI(TAG, "START 短按，退出音频播放");
                audio_player_stop();
                return ESP_OK;
            }
            if (a_edge) {
                ESP_LOGI(TAG, "A 键，跳到上一首");
                audio_player_stop();
                skip_to_prev = true;
                track_done = true;
            }
            if (b_edge) {
                ESP_LOGI(TAG, "B 键，跳到下一首");
                audio_player_stop();
                skip_to_next = true;
                track_done = true;
            }

            last_pad = pad;

            vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));
        }

        /* Advance to next or previous track */
        if (skip_to_prev) {
            if (current_track > 0) {
                current_track--;
            } else {
                current_track = track_count - 1;  /* wrap to last track */
            }
        } else {
            /* skip_to_next or natural finish → advance to next */
            current_track++;
        }
    }

    ESP_LOGI(TAG, "目录内所有音频播放完毕");
    return ESP_OK;
}

static esp_err_t main_wait_rom_menu_selection(size_t *selected)
{
    if (selected == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(main_key_config_input(), TAG, "config menu key failed");
    *selected = 0;
    ESP_RETURN_ON_ERROR(main_draw_rom_menu(*selected), TAG, "draw rom menu failed");

    if (s_rom_total_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    bool stable_pressed = false;
    bool last_raw_pressed = false;
    uint8_t same_count = 0;
    int64_t press_start_us = 0;
    bool boot_stable_pressed = false;
    bool boot_last_raw_pressed = false;
    uint8_t boot_same_count = 0;
    uint8_t last_web_joypad = main_get_merged_joypad();
    main_menu_action_t repeat_action = MAIN_MENU_ACTION_NONE;
    int64_t next_repeat_us = 0;
    int64_t last_activity_us = esp_timer_get_time();
    int64_t next_clock_update_us = 0;
    bool clock_visible = false;
    bool clock_sync_scheduled = false;
    int64_t clock_sync_due_us = 0;

    while (1) {
        bool raw_pressed = gpio_get_level(MAIN_KEY_GPIO) == 0;
        bool boot_raw_pressed = gpio_get_level(MAIN_BOOT_GPIO) == 0;
        uint8_t web_joypad = main_get_merged_joypad();
        uint8_t web_pressed_edges = (uint8_t)(last_web_joypad & (uint8_t)~web_joypad);
        last_web_joypad = web_joypad;
        int64_t now_us = esp_timer_get_time();

        if (clock_visible) {
            if (raw_pressed || boot_raw_pressed || web_joypad != 0xff) {
                clock_visible = false;
                repeat_action = MAIN_MENU_ACTION_NONE;
                next_repeat_us = 0;
                last_activity_us = now_us;
                last_web_joypad = web_joypad;
                clock_sync_scheduled = false;
                ESP_RETURN_ON_ERROR(main_draw_rom_menu(*selected), TAG, "restore rom menu failed");
                vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));
                continue;
            }

            /* 屏保期间按节流策略触发 WiFi 自动对时（不阻塞按键响应）
             * 从未成功对时则直接放行（首次不受 30 分钟限制）；
             * 已对过时则距上次超过 30 分钟才再调度。
             * 对时任务运行中不重新调度，避免完成后立即触发第二次。
             */
            if (!clock_sync_scheduled && !s_clock_sync_busy) {
                bool need_sync = !s_clock_ever_synced ||
                                 (now_us - s_clock_last_sync_us >= MAIN_CLOCK_WIFI_SYNC_INTERVAL_MS * 1000LL);
                if (need_sync) {
                    clock_sync_scheduled = true;
                    clock_sync_due_us = now_us + MAIN_CLOCK_WIFI_SYNC_INITIAL_DELAY_MS * 1000LL;
                }
            }
            if (clock_sync_scheduled && now_us >= clock_sync_due_us) {
                clock_sync_scheduled = false;
                main_clock_wifi_sync_start();
            }

            if (now_us >= next_clock_update_us) {
                ESP_RETURN_ON_ERROR(main_draw_clock_screen(), TAG, "update clock screen failed");
                next_clock_update_us = now_us + MAIN_CLOCK_UPDATE_MS * 1000LL;
            }

            vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));
            continue;
        }

        if (now_us - last_activity_us >= MAIN_CLOCK_IDLE_MS * 1000LL) {
            ESP_RETURN_ON_ERROR(main_draw_clock_screen(), TAG, "draw idle clock failed");
            clock_visible = true;
            next_clock_update_us = now_us + MAIN_CLOCK_UPDATE_MS * 1000LL;
            vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));
            continue;
        }

        if ((web_pressed_edges & (MAIN_GB_BUTTON_A | MAIN_GB_BUTTON_START)) != 0) {
            last_activity_us = now_us;
            bool rom_selected = false;
            bool audio_selected = false;
            ESP_RETURN_ON_ERROR(main_activate_rom_browser_entry(selected, &rom_selected, &audio_selected),
                                TAG,
                                "activate web menu entry failed");
            if (rom_selected) {
                return ESP_OK;
            }
            if (audio_selected) {
                /* Play audio file */
                char audio_path[MAIN_GB_ROM_PATH_MAX];
                strlcpy(audio_path, s_rom_entries[*selected - s_rom_page_start].path, sizeof(audio_path));
                board_shtc3_deinit();
                ESP_RETURN_ON_ERROR(main_wait_audio_playback(audio_path), TAG, "audio playback failed");
                ESP_RETURN_ON_ERROR(main_draw_rom_menu(*selected), TAG, "redraw rom menu after audio failed");
            }
        }

        if ((web_pressed_edges & MAIN_GB_BUTTON_B) != 0) {
            last_activity_us = now_us;
            ESP_RETURN_ON_ERROR(main_return_rom_browser_parent(selected), TAG, "return parent dir failed");
        }

        main_menu_action_t edge_action = MAIN_MENU_ACTION_NONE;
        if ((web_pressed_edges & MAIN_GB_BUTTON_RIGHT) != 0) {
            edge_action = MAIN_MENU_ACTION_PAGE_DOWN;
        } else if ((web_pressed_edges & MAIN_GB_BUTTON_LEFT) != 0) {
            edge_action = MAIN_MENU_ACTION_PAGE_UP;
        } else if ((web_pressed_edges & MAIN_GB_BUTTON_DOWN) != 0) {
            edge_action = MAIN_MENU_ACTION_DOWN;
        } else if ((web_pressed_edges & MAIN_GB_BUTTON_UP) != 0) {
            edge_action = MAIN_MENU_ACTION_UP;
        }

        if (edge_action != MAIN_MENU_ACTION_NONE) {
            last_activity_us = now_us;
            ESP_RETURN_ON_ERROR(main_apply_rom_menu_action(selected, edge_action), TAG, "apply web menu action failed");
        }

        main_menu_action_t held_action = MAIN_MENU_ACTION_NONE;
        if ((web_joypad & MAIN_GB_BUTTON_RIGHT) == 0) {
            held_action = MAIN_MENU_ACTION_PAGE_DOWN;
        } else if ((web_joypad & MAIN_GB_BUTTON_LEFT) == 0) {
            held_action = MAIN_MENU_ACTION_PAGE_UP;
        } else if ((web_joypad & MAIN_GB_BUTTON_DOWN) == 0) {
            held_action = MAIN_MENU_ACTION_DOWN;
        } else if ((web_joypad & MAIN_GB_BUTTON_UP) == 0) {
            held_action = MAIN_MENU_ACTION_UP;
        }

        if (held_action == MAIN_MENU_ACTION_NONE) {
            repeat_action = MAIN_MENU_ACTION_NONE;
            next_repeat_us = 0;
        } else if (held_action != repeat_action) {
            repeat_action = held_action;
            next_repeat_us = now_us + MAIN_MENU_REPEAT_DELAY_MS * 1000LL;
        } else if (now_us >= next_repeat_us) {
            last_activity_us = now_us;
            ESP_RETURN_ON_ERROR(main_apply_rom_menu_action(selected, held_action), TAG, "repeat web menu action failed");
            next_repeat_us = now_us + MAIN_MENU_REPEAT_INTERVAL_MS * 1000LL;
        }

        if (raw_pressed == last_raw_pressed) {
            if (same_count < MAIN_KEY_DEBOUNCE_COUNT) {
                same_count++;
            }
        } else {
            same_count = 0;
            last_raw_pressed = raw_pressed;
        }

        if (same_count >= MAIN_KEY_DEBOUNCE_COUNT && raw_pressed != stable_pressed) {
            stable_pressed = raw_pressed;

            if (stable_pressed) {
                press_start_us = esp_timer_get_time();
                last_activity_us = now_us;
            } else {
                int64_t press_ms = (esp_timer_get_time() - press_start_us) / 1000;
                last_activity_us = now_us;
                if (press_ms >= MAIN_MENU_LONG_PRESS_MS) {
                    /* KEY 长按 = 进入蓝牙手柄配置页 */
                    ESP_LOGI(TAG, "KEY 长按，进入蓝牙手柄配置");
                    ESP_RETURN_ON_ERROR(main_wait_bt_config(), TAG, "bt config failed");
                    /* 配置完成返回 ROM 菜单（保持已连接手柄状态） */
                    ESP_RETURN_ON_ERROR(main_draw_rom_menu(*selected), TAG, "redraw rom menu after bt config failed");
                    /* 重置按键去抖状态，避免配置页残留输入 */
                    stable_pressed = false;
                    last_raw_pressed = false;
                    same_count = 0;
                    boot_stable_pressed = false;
                    boot_last_raw_pressed = false;
                    boot_same_count = 0;
                    continue;
                }

                *selected = (*selected + 1) % s_rom_total_count;
                ESP_RETURN_ON_ERROR(main_draw_rom_menu(*selected), TAG, "redraw rom menu failed");
            }
        }

        /* BOOT(GPIO0) 去抖：短按 = 确认打开 ROM/目录 */
        if (boot_raw_pressed == boot_last_raw_pressed) {
            if (boot_same_count < MAIN_KEY_DEBOUNCE_COUNT) {
                boot_same_count++;
            }
        } else {
            boot_same_count = 0;
            boot_last_raw_pressed = boot_raw_pressed;
        }

        if (boot_same_count >= MAIN_KEY_DEBOUNCE_COUNT && boot_raw_pressed != boot_stable_pressed) {
            boot_stable_pressed = boot_raw_pressed;
            if (!boot_stable_pressed) {
                last_activity_us = now_us;
                bool rom_selected = false;
                bool audio_selected = false;
                ESP_RETURN_ON_ERROR(main_activate_rom_browser_entry(selected, &rom_selected, &audio_selected),
                                    TAG,
                                    "activate boot menu entry failed");
                if (rom_selected) {
                    return ESP_OK;
                }
                if (audio_selected) {
                    char audio_path[MAIN_GB_ROM_PATH_MAX];
                    strlcpy(audio_path, s_rom_entries[*selected - s_rom_page_start].path, sizeof(audio_path));
                    board_shtc3_deinit();
                    ESP_RETURN_ON_ERROR(main_wait_audio_playback(audio_path), TAG, "audio playback failed");
                    ESP_RETURN_ON_ERROR(main_draw_rom_menu(*selected), TAG, "redraw rom menu after audio failed");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));
    }
}

static void main_wait_game_exit_button(void)
{
    ESP_ERROR_CHECK(main_key_config_input());

    bool stable_pressed = false;
    bool last_raw_pressed = false;
    uint8_t same_count = 0;
    uint8_t last_merged = main_get_merged_joypad();

    while (1) {
        bool raw_pressed = gpio_get_level(MAIN_KEY_GPIO) == 0;

        /*
         * 持续将合并后的手柄状态（web + bluetooth）推送给模拟器。
         * web_gamepad 的 WebSocket 回调也会直接 set，但这里用合并值覆盖，
         * 确保蓝牙手柄的输入不被丢失。两者都是低有效，AND 合并。
         */
        uint8_t merged = main_get_merged_joypad();
        if (merged != last_merged) {
            last_merged = merged;
            gb_emu_set_joypad(merged);
            gbc_emu_set_joypad(merged);
        }

        if (raw_pressed == last_raw_pressed) {
            if (same_count < MAIN_KEY_DEBOUNCE_COUNT) {
                same_count++;
            }
        } else {
            same_count = 0;
            last_raw_pressed = raw_pressed;
        }

        if (same_count >= MAIN_KEY_DEBOUNCE_COUNT && raw_pressed != stable_pressed) {
            stable_pressed = raw_pressed;
            if (stable_pressed) {
                ESP_LOGI(TAG, "GPIO18 触发退出游戏，返回 ROM 选择界面");
                return;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_KEY_SCAN_INTERVAL_MS));
    }
}

void app_main(void) {
    esp_err_t ret = board_sdmmc_mount();
    if (ret == ESP_OK) {
#if MAIN_RUN_SD_SELF_TEST
        board_sdmmc_self_test();
#endif
        (void)main_font_bin_init_embedded();
        (void)main_scan_rom_browser(MAIN_GB_ROM_DIR);
    } else {
        ESP_LOGE(TAG, "SD 卡加载失败：%s", esp_err_to_name(ret));
    }

    ret = board_rlcd_init();
    if (ret == ESP_OK) {
#if MAIN_SHOW_GB_EMU
#if MAIN_ENABLE_WEB_GAMEPAD
        /*
         * Web 手柄本质上也是 WiFi AP。
         * 这里必须在 ROM 菜单前启动，否则菜单等待按键选择时，
         * 手机会搜不到 ESP32 的 WiFi。
         */
        ESP_ERROR_CHECK(web_gamepad_start());
#endif
#if MAIN_ENABLE_BT_GAMEPAD
        /*
         * 蓝牙手柄与 WiFi 网页手柄并存。
         * ESP32-S3 支持 BLE，可连接 BLE HID 手柄。
         * 蓝牙和 WiFi 共存（ESP32-S3 支持 BLE + WiFi coexistence）。
         */
        esp_err_t bt_ret = bt_gamepad_start();
        if (bt_ret != ESP_OK) {
            ESP_LOGW(TAG, "蓝牙手柄启动失败：%s（不影响 WiFi 手柄使用）", esp_err_to_name(bt_ret));
        }
#endif
#if MAIN_ENABLE_BT_GAMEPAD
        /*
         * 开机时先尝试自动连接已配对的蓝牙手柄。
         * 如果 NVS 中有完整的十键映射和设备指纹，且扫描能匹配到设备，
         * 则自动连接并跳过蓝牙配置页，直接进入 ROM 菜单。
         * 自动连接失败时回退到手动配置流程。
         */
        if (bt_gamepad_try_auto_connect() != ESP_OK) {
            ESP_LOGI(TAG, "自动连接失败，进入手动蓝牙配置");
            ESP_ERROR_CHECK(main_wait_bt_config());
        } else {
            ESP_LOGI(TAG, "蓝牙手柄已自动连接，跳过配置页");
        }
#endif
#if MAIN_LOAD_GB_ROM
        while (1) {
            size_t selected_rom = 0;
            ret = main_wait_rom_menu_selection(&selected_rom);
            bool selected_is_gbc = false;
            const char *selected_path = NULL;
            char selected_path_storage[MAIN_GB_ROM_PATH_MAX] = {0};

            if (ret == ESP_OK) {
                ret = main_ensure_rom_browser_page(selected_rom);
                if (ret == ESP_OK) {
                    strlcpy(selected_path_storage,
                            s_rom_entries[selected_rom - s_rom_page_start].path,
                            sizeof(selected_path_storage));
                    selected_path = selected_path_storage;
                    selected_is_gbc = main_is_gbc_rom_path(selected_path);
                }

                if (ret == ESP_OK && !selected_is_gbc) {
                    ret = gb_emu_load_rom(selected_path, &s_gb_rom);
                }

                if (ret == ESP_OK && !selected_is_gbc) {
                    s_gb_rom_loaded = true;
                    gb_emu_log_rom_info(&s_gb_rom);
                } else if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "ROM 加载失败：%s", esp_err_to_name(ret));
                } else {
                    ESP_LOGI(TAG, "GBC ROM 将使用 gnuboy 启动：%s", selected_path);
                }
            } else {
                ESP_LOGE(TAG, "未选择 ROM：%s", esp_err_to_name(ret));
            }

            if (ret == ESP_OK && selected_path != NULL && selected_is_gbc) {
                board_shtc3_deinit();
                ESP_ERROR_CHECK(gbc_emu_start_file(selected_path));
                ESP_LOGI(TAG, "GBC 模拟器已启动，按 GPIO18 返回 ROM 选择界面");
                main_wait_game_exit_button();
                ESP_ERROR_CHECK(gbc_emu_stop());
                board_speaker_deinit();
                char current_dir[MAIN_GB_ROM_PATH_MAX];
                strlcpy(current_dir, s_rom_current_dir, sizeof(current_dir));
                (void)main_scan_rom_browser(current_dir);
            } else if (s_gb_rom_loaded) {
                board_shtc3_deinit();
                ESP_ERROR_CHECK(gb_emu_start(&s_gb_rom));
                ESP_LOGI(TAG, "GB 模拟器已启动，按 GPIO18 返回 ROM 选择界面");
                main_wait_game_exit_button();
                ESP_ERROR_CHECK(gb_emu_stop());
                board_speaker_deinit();
                gb_emu_free_rom(&s_gb_rom);
                s_gb_rom_loaded = false;
                char current_dir[MAIN_GB_ROM_PATH_MAX];
                strlcpy(current_dir, s_rom_current_dir, sizeof(current_dir));
                (void)main_scan_rom_browser(current_dir);
            } else {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
#else
        ESP_LOGE(TAG, "GB ROM 加载未启用");
#endif
#elif MAIN_SHOW_RLCD_ORIENTATION_TEST
        ESP_ERROR_CHECK(rlcd_test_pattern_draw_orientation());
        ESP_LOGI(TAG, "RLCD 方向测试图案已显示");
#else
        ESP_ERROR_CHECK(board_lvgl_init());
#if MAIN_SHOW_LVGL_RESPONSIVE_TEST
        ESP_ERROR_CHECK(board_lvgl_create_test_screen());
        ESP_LOGI(TAG, "LVGL 响应式布局测试界面已启动");
#else
        ESP_ERROR_CHECK(audio_wave_ui_start());
        ESP_LOGI(TAG, "音频波形测试界面已启动");
#endif
#endif
        ESP_ERROR_CHECK(main_key_init());
    } else {
        ESP_LOGE(TAG, "RLCD 初始化失败：%s", esp_err_to_name(ret));
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
}
