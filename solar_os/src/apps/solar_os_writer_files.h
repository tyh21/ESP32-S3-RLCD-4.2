#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef enum {
    SOLAR_OS_WRITER_FILE_FAULT_NONE,
    SOLAR_OS_WRITER_FILE_FAULT_OPEN,
    SOLAR_OS_WRITER_FILE_FAULT_WRITE,
    SOLAR_OS_WRITER_FILE_FAULT_FLUSH,
    SOLAR_OS_WRITER_FILE_FAULT_BACKUP_RENAME,
    SOLAR_OS_WRITER_FILE_FAULT_FINAL_RENAME,
    SOLAR_OS_WRITER_FILE_FAULT_VERIFY,
} solar_os_writer_file_fault_t;

esp_err_t solar_os_writer_safe_replace(const char *path,
                                       const char *data,
                                       size_t len,
                                       solar_os_writer_file_fault_t fault);

