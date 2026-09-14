#include "solar_os_writer_files.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "solar_os_storage.h"

static bool writer_file_suffix(const char *path,
                               const char *suffix,
                               char *out,
                               size_t out_len)
{
    const int written = snprintf(out, out_len, "%s%s", path, suffix);
    return written >= 0 && (size_t)written < out_len;
}

static bool writer_file_matches(const char *path, const char *data, size_t len)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    char chunk[512];
    size_t offset = 0;
    bool matches = true;
    while (offset < len) {
        size_t take = len - offset;
        if (take > sizeof(chunk)) {
            take = sizeof(chunk);
        }
        if (fread(chunk, 1, take, file) != take ||
            memcmp(chunk, &data[offset], take) != 0) {
            matches = false;
            break;
        }
        offset += take;
    }
    if (matches && fgetc(file) != EOF) {
        matches = false;
    }
    if (fclose(file) != 0) {
        matches = false;
    }
    return matches;
}

esp_err_t solar_os_writer_safe_replace(const char *path,
                                       const char *data,
                                       size_t len,
                                       solar_os_writer_file_fault_t fault)
{
    if (path == NULL || path[0] == '\0' || (data == NULL && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    char staged[SOLAR_OS_STORAGE_PATH_MAX];
    char backup[SOLAR_OS_STORAGE_PATH_MAX];
    if (!writer_file_suffix(path, ".writer.tmp", staged, sizeof(staged)) ||
        !writer_file_suffix(path, ".writer.bak", backup, sizeof(backup))) {
        return ESP_ERR_INVALID_SIZE;
    }

    (void)remove(staged);
    if (fault == SOLAR_OS_WRITER_FILE_FAULT_OPEN) {
        return ESP_FAIL;
    }
    FILE *file = fopen(staged, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    esp_err_t ret = ESP_OK;
    if (fault == SOLAR_OS_WRITER_FILE_FAULT_WRITE ||
        (len > 0 && fwrite(data, 1, len, file) != len)) {
        ret = ESP_FAIL;
    }
    if (ret == ESP_OK &&
        (fault == SOLAR_OS_WRITER_FILE_FAULT_FLUSH ||
         fflush(file) != 0 || fsync(fileno(file)) != 0)) {
        ret = ESP_FAIL;
    }
    if (fclose(file) != 0 && ret == ESP_OK) {
        ret = ESP_FAIL;
    }
    if (ret != ESP_OK) {
        (void)remove(staged);
        return ret;
    }

    struct stat st;
    const bool had_original = stat(path, &st) == 0;
    (void)remove(backup);
    if (had_original) {
        if (fault == SOLAR_OS_WRITER_FILE_FAULT_BACKUP_RENAME ||
            rename(path, backup) != 0) {
            (void)remove(staged);
            return ESP_FAIL;
        }
    } else if (fault == SOLAR_OS_WRITER_FILE_FAULT_BACKUP_RENAME) {
        (void)remove(staged);
        return ESP_FAIL;
    }

    if (fault == SOLAR_OS_WRITER_FILE_FAULT_FINAL_RENAME || rename(staged, path) != 0) {
        if (had_original) {
            (void)rename(backup, path);
        }
        (void)remove(staged);
        return ESP_FAIL;
    }

    if (fault == SOLAR_OS_WRITER_FILE_FAULT_VERIFY ||
        !writer_file_matches(path, data != NULL ? data : "", len)) {
        (void)remove(path);
        if (had_original) {
            (void)rename(backup, path);
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (had_original) {
        (void)remove(backup);
    }
    return ESP_OK;
}

