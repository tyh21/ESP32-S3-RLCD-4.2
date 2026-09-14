#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_NVS_BACKUP_DEFAULT_PATH "/.solar/nvs.bin"

typedef struct {
    size_t partition_size;
    size_t file_size;
    uint32_t crc32;
    bool reboot_required;
} solar_os_nvs_backup_result_t;

esp_err_t solar_os_nvs_backup_create(
    const char *path,
    solar_os_nvs_backup_result_t *result);

/* The caller must reboot when result->reboot_required is true. */
esp_err_t solar_os_nvs_backup_restore(
    const char *path,
    solar_os_nvs_backup_result_t *result);
