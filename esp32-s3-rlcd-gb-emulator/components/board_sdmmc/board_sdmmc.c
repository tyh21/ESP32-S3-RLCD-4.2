#include "board_sdmmc.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "board_sdmmc";

/*
 * 这组引脚来自 ESP32-S3-RLCD-4.2 官方 06_SD_Card 示例：
 *
 *   CLK = GPIO38
 *   CMD = GPIO21
 *   D0  = GPIO39
 *
 * 官方示例使用 esp_vfs_fat_sdmmc_mount()，并且只配置 CLK/CMD/D0，
 * 因此这里按 SDMMC 1-bit 模式实现。
 *
 * 注意：
 * - 1-bit SDMMC 需要 CLK/CMD/D0 三根信号线；
 * - 4-bit SDMMC 需要 CLK/CMD/D0/D1/D2/D3 六根信号线；
 * - 原理图里出现 SDCS/MOSI/SCK/MISO 这类网络名，但官方 ESP-IDF 示例
 *   实际按 SDMMC 1-bit 初始化，本 BSP 以官方可运行示例为准。
 *
 * 这组引脚不会占用 UART0 的 U0RXD/U0TXD，因此 USB 负责烧录、
 * UART0 负责日志输出的用法可以同时成立。
 */
#define BOARD_SDMMC_PIN_CLK     38
#define BOARD_SDMMC_PIN_CMD     21
#define BOARD_SDMMC_PIN_D0      39
#define BOARD_SDMMC_BUS_WIDTH   1

/*
 * 保存挂载成功后的 SD 卡句柄。
 *
 * esp_vfs_fat_sdmmc_mount() 成功后会把 sdmmc_card_t 指针写到这里。
 * 之后读卡信息、检查卡状态、卸载文件系统都需要它。
 */
static sdmmc_card_t *s_card = NULL;

bool board_sdmmc_is_mounted(void)
{
    return s_card != NULL;
}

sdmmc_card_t *board_sdmmc_get_card(void)
{
    return s_card;
}

esp_err_t board_sdmmc_mount(void)
{
    if (s_card != NULL) {
        ESP_LOGW(TAG, "SD card already mounted at %s", BOARD_SDMMC_MOUNT_POINT);
        return ESP_OK;
    }

    /*
     * FATFS 挂载配置。
     *
     * format_if_mount_failed = false：
     *   挂载失败时不要自动格式化 SD 卡。调试阶段这样更安全，避免误删卡内数据。
     *
     * max_files = 5：
     *   同时最多打开 5 个文件。简单测试足够用。
     *
     * allocation_unit_size：
     *   FAT 文件系统分配单元大小。16KB 是 ESP-IDF 示例常用值。
     */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    /*
     * SDMMC Host 配置。
     *
     * SDMMC_HOST_DEFAULT() 会选择 ESP32-S3 内部的 SDMMC Host 外设。
     */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    /*
     * 如果识别不稳定，可以先降低频率验证硬件连线。
     * 如果识别不稳定，可以先降低频率验证硬件连线。
     */
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    /*
     * SDMMC Slot 配置。
     *
     * 这里只配置 1-bit 模式需要的 CLK/CMD/D0。
     * D1/D2/D3 不配置，避免占用其他功能引脚。
     */
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = BOARD_SDMMC_BUS_WIDTH;
    slot_config.clk = BOARD_SDMMC_PIN_CLK;
    slot_config.cmd = BOARD_SDMMC_PIN_CMD;
    slot_config.d0 = BOARD_SDMMC_PIN_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting SD card at %s", BOARD_SDMMC_MOUNT_POINT);
    ESP_LOGI(TAG, "SDMMC 1-bit pins: CLK=%d CMD=%d D0=%d",
             BOARD_SDMMC_PIN_CLK,
             BOARD_SDMMC_PIN_CMD,
             BOARD_SDMMC_PIN_D0);

    /*
     * 这是整个 SD 卡识别流程的核心函数。
     *
     * 它内部会做几件事：
     * 1. 初始化 SDMMC Host；
     * 2. 按 slot_config 配置 GPIO 和 SDMMC 信号；
     * 3. 和 SD 卡通信，读取卡信息；
     * 4. 挂载 FAT 文件系统；
     * 5. 成功后把 sdmmc_card_t 写入 s_card。
     */
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        BOARD_SDMMC_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &s_card);

    if (ret != ESP_OK) {
        s_card = NULL;
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Check: card inserted, FAT32 format, SDMMC pins, and pull-up resistors");
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted successfully");

    /*
     * 打印卡信息。
     *
     * 如果你在串口里看到卡名、类型、容量、速度等信息，
     * 就说明 ESP32-S3 已经成功识别 SD 卡。
     */
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t board_sdmmc_unmount(void)
{
    if (s_card == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(BOARD_SDMMC_MOUNT_POINT, s_card);
    if (ret == ESP_OK) {
        s_card = NULL;
        ESP_LOGI(TAG, "SD card unmounted");
    } else {
        ESP_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t board_sdmmc_write_text_file(const char *path, const char *text)
{
    if (s_card == NULL) {
        ESP_LOGE(TAG, "SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (path == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * 在访问文件前检查一次 SD 卡状态。
     * 如果卡被拔出，sdmmc_get_status() 通常会返回错误。
     */
    esp_err_t ret = sdmmc_get_status(s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card status error: %s", esp_err_to_name(ret));
        return ret;
    }

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", path);
        return ESP_FAIL;
    }

    int written = fprintf(file, "%s", text);
    fclose(file);

    if (written < 0) {
        ESP_LOGE(TAG, "Failed to write file: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Wrote %d bytes to %s", written, path);
    return ESP_OK;
}

esp_err_t board_sdmmc_read_text_file(const char *path)
{
    if (s_card == NULL) {
        ESP_LOGE(TAG, "SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = sdmmc_get_status(s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card status error: %s", esp_err_to_name(ret));
        return ret;
    }

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", path);
        return ESP_FAIL;
    }

    char line[128] = {0};
    char *result = fgets(line, sizeof(line), file);
    fclose(file);

    if (result == NULL) {
        ESP_LOGE(TAG, "Failed to read file: %s", path);
        return ESP_FAIL;
    }

    /*
     * 读出来能打印，说明：
     * 1. SDMMC 通信正常；
     * 2. FAT 文件系统挂载正常；
     * 3. 标准 C 文件接口 fopen/fgets/fprintf 可以正常访问 /sdcard。
     */
    ESP_LOGI(TAG, "Read from %s: %s", path, line);
    return ESP_OK;
}

esp_err_t board_sdmmc_self_test(void)
{
    const char *path = BOARD_SDMMC_MOUNT_POINT "/sdmmc_test.txt";
    const char *text = "Hello from ESP32-S3 SDMMC\r\n";

    esp_err_t ret = board_sdmmc_write_text_file(path, text);
    if (ret != ESP_OK) {
        return ret;
    }

    return board_sdmmc_read_text_file(path);
}
