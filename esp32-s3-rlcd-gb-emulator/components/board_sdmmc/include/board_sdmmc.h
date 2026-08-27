#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 板级 SD 卡驱动的挂载点。
 *
 * 挂载成功后，SD 卡根目录会映射到这个路径。
 * 例如：
 *   /sdcard/hello.txt
 * 就表示 SD 卡根目录下的 hello.txt 文件。
 */
#define BOARD_SDCARD_MOUNT_POINT "/sdcard"

/* 兼容旧名字：后续新代码建议使用 BOARD_SDCARD_MOUNT_POINT。 */
#define BOARD_SDMMC_MOUNT_POINT BOARD_SDCARD_MOUNT_POINT

/*
 * 初始化并挂载 SD 卡。
 *
 * 这个函数会完成：
 * 1. 配置 ESP32-S3 的 SDMMC Host；
 * 2. 配置本开发板 SD 卡座连接到 ESP32-S3 的 SDMMC GPIO；
 * 3. 挂载 FAT 文件系统到 /sdcard；
 * 4. 挂载成功后打印 SD 卡信息。
 *
 * 返回：
 *   ESP_OK      SD 卡识别并挂载成功；
 *   其他错误码  初始化、识别或挂载失败。
 */
esp_err_t board_sdmmc_mount(void);

/*
 * 卸载 SD 卡。
 *
 * 如果 SD 卡已经挂载，本函数会卸载 FAT 文件系统并释放 SDMMC/SD 卡资源。
 * 如果 SD 卡本来就没有挂载，本函数直接返回 ESP_OK。
 */
esp_err_t board_sdmmc_unmount(void);

/*
 * 查询 SD 卡是否已经挂载。
 */
bool board_sdmmc_is_mounted(void);

/*
 * 获取 ESP-IDF 的 SD 卡对象。
 *
 * 这个对象里保存了卡类型、容量、速度等信息。
 * 如果尚未挂载，返回 NULL。
 */
sdmmc_card_t *board_sdmmc_get_card(void);

/*
 * 写入一个文本文件。
 *
 * path 必须是 /sdcard 开头的完整路径，例如：
 *   /sdcard/test.txt
 */
esp_err_t board_sdmmc_write_text_file(const char *path, const char *text);

/*
 * 读取一个文本文件并打印到日志。
 *
 * 这是为了做最小验证：确认 SD 卡不仅能识别，还能读写 FAT 文件。
 */
esp_err_t board_sdmmc_read_text_file(const char *path);

/*
 * 最小自检：
 * 1. 向 /sdcard/sdspi_test.txt 写入一行文本；
 * 2. 再把这个文件读出来并打印。
 */
esp_err_t board_sdmmc_self_test(void);

#ifdef __cplusplus
}
#endif
