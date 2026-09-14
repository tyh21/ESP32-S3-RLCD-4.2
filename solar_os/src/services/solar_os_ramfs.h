#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_RAMFS_MAX_MOUNTS 4
#define SOLAR_OS_RAMFS_MOUNT_POINT_MAX 16

typedef struct {
    char mount_point[SOLAR_OS_RAMFS_MOUNT_POINT_MAX];
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    size_t file_count;
    size_t dir_count;
    size_t open_count;
} solar_os_ramfs_info_t;

esp_err_t solar_os_ramfs_mount(const char *mount_point, size_t quota_bytes);
esp_err_t solar_os_ramfs_unmount(const char *mount_point);
size_t solar_os_ramfs_mount_count(void);
bool solar_os_ramfs_get_info(size_t index, solar_os_ramfs_info_t *info);
bool solar_os_ramfs_path_has_mount_prefix(const char *path);
esp_err_t solar_os_ramfs_path_mount_point(const char *path,
                                          char *mount_point,
                                          size_t mount_point_len);
esp_err_t solar_os_ramfs_get_usage_for_path(const char *path,
                                            uint64_t *total_bytes,
                                            uint64_t *used_bytes,
                                            uint64_t *free_bytes);
