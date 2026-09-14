#include "solar_os_nvs_backup.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "solar_os_storage.h"

#define NVS_BACKUP_FORMAT_VERSION 1U
#define NVS_BACKUP_IO_BUFFER_SIZE 1024U

typedef struct {
    uint8_t magic[8];
    uint32_t format_version;
    uint32_t header_size;
    uint32_t partition_address;
    uint32_t partition_size;
    uint32_t payload_crc32;
    char partition_label[16];
} solar_os_nvs_backup_header_t;

_Static_assert(sizeof(solar_os_nvs_backup_header_t) == 44U,
               "NVS backup header format changed");

static const uint8_t nvs_backup_magic[8] = {
    'S', 'O', 'L', 'N', 'V', 'S', '1', '\0',
};
static portMUX_TYPE nvs_backup_lock = portMUX_INITIALIZER_UNLOCKED;
static bool nvs_backup_busy;

static bool nvs_backup_begin(void)
{
    portENTER_CRITICAL(&nvs_backup_lock);
    const bool available = !nvs_backup_busy;
    if (available) {
        nvs_backup_busy = true;
    }
    portEXIT_CRITICAL(&nvs_backup_lock);
    return available;
}

static void nvs_backup_end(void)
{
    portENTER_CRITICAL(&nvs_backup_lock);
    nvs_backup_busy = false;
    portEXIT_CRITICAL(&nvs_backup_lock);
}

static uint32_t nvs_backup_crc32_update(uint32_t crc,
                                        const void *data,
                                        size_t len)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8U; bit++) {
            crc = (crc & 1U) != 0U ?
                (crc >> 1U) ^ 0xedb88320UL : crc >> 1U;
        }
    }
    return crc;
}

static const esp_partition_t *nvs_backup_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_DATA_NVS,
                                    "nvs");
}

static esp_err_t nvs_backup_reinitialize(void)
{
    const esp_err_t error = nvs_flash_init();
    return error == ESP_ERR_INVALID_STATE ? ESP_OK : error;
}

static bool nvs_backup_path_with_suffix(const char *path,
                                        const char *suffix,
                                        char *output,
                                        size_t output_len)
{
    const int written = snprintf(output, output_len, "%s%s", path, suffix);
    return written >= 0 && (size_t)written < output_len;
}

static esp_err_t nvs_backup_replace_file(const char *path,
                                         const char *temporary)
{
    char backup[SOLAR_OS_STORAGE_PATH_MAX + 8U];
    if (!nvs_backup_path_with_suffix(path, ".bak", backup, sizeof(backup))) {
        return ESP_ERR_INVALID_SIZE;
    }

    struct stat status;
    const bool existed = stat(path, &status) == 0;
    (void)remove(backup);
    if (existed && rename(path, backup) != 0) {
        return ESP_FAIL;
    }
    if (rename(temporary, path) != 0) {
        if (existed) {
            (void)rename(backup, path);
        }
        return ESP_FAIL;
    }
    (void)remove(backup);
    return ESP_OK;
}

static esp_err_t nvs_backup_write_snapshot(
    const esp_partition_t *partition,
    const char *path,
    solar_os_nvs_backup_result_t *result)
{
    char temporary[SOLAR_OS_STORAGE_PATH_MAX + 8U];
    if (!nvs_backup_path_with_suffix(path,
                                     ".tmp",
                                     temporary,
                                     sizeof(temporary))) {
        return ESP_ERR_INVALID_SIZE;
    }
    (void)remove(temporary);

    solar_os_nvs_backup_header_t header = {
        .format_version = NVS_BACKUP_FORMAT_VERSION,
        .header_size = sizeof(header),
        .partition_address = partition->address,
        .partition_size = partition->size,
    };
    memcpy(header.magic, nvs_backup_magic, sizeof(header.magic));
    strlcpy(header.partition_label,
            partition->label,
            sizeof(header.partition_label));

    esp_err_t error = nvs_flash_deinit();
    if (error != ESP_OK) {
        return error;
    }

    FILE *file = fopen(temporary, "wb");
    if (file == NULL) {
        error = ESP_FAIL;
    }

    if (error == ESP_OK &&
        fwrite(&header, sizeof(header), 1U, file) != 1U) {
        error = ESP_FAIL;
    }

    uint8_t buffer[NVS_BACKUP_IO_BUFFER_SIZE];
    uint32_t crc = 0xffffffffUL;
    for (size_t offset = 0; error == ESP_OK && offset < partition->size;) {
        const size_t remaining = partition->size - offset;
        const size_t count = remaining < sizeof(buffer) ?
            remaining : sizeof(buffer);
        error = esp_partition_read(partition, offset, buffer, count);
        if (error == ESP_OK && fwrite(buffer, 1U, count, file) != count) {
            error = ESP_FAIL;
        }
        if (error == ESP_OK) {
            crc = nvs_backup_crc32_update(crc, buffer, count);
            offset += count;
        }
    }

    header.payload_crc32 = ~crc;
    if (error == ESP_OK &&
        (fseek(file, 0, SEEK_SET) != 0 ||
         fwrite(&header, sizeof(header), 1U, file) != 1U ||
         fflush(file) != 0 || fsync(fileno(file)) != 0)) {
        error = ESP_FAIL;
    }
    if (file != NULL && fclose(file) != 0 && error == ESP_OK) {
        error = ESP_FAIL;
    }

    const esp_err_t init_error = nvs_backup_reinitialize();
    if (init_error != ESP_OK) {
        result->reboot_required = true;
        error = init_error;
    }
    if (error != ESP_OK) {
        (void)remove(temporary);
        return error;
    }

    error = nvs_backup_replace_file(path, temporary);
    if (error == ESP_OK) {
        result->partition_size = partition->size;
        result->file_size = sizeof(header) + partition->size;
        result->crc32 = header.payload_crc32;
    } else {
        (void)remove(temporary);
    }
    return error;
}

static esp_err_t nvs_backup_validate_header(
    const solar_os_nvs_backup_header_t *header,
    const esp_partition_t *partition,
    size_t file_size)
{
    if (memcmp(header->magic, nvs_backup_magic, sizeof(header->magic)) != 0 ||
        header->format_version != NVS_BACKUP_FORMAT_VERSION ||
        header->header_size != sizeof(*header) ||
        strnlen(header->partition_label,
                sizeof(header->partition_label)) ==
            sizeof(header->partition_label) ||
        strcmp(header->partition_label, partition->label) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (header->partition_address != partition->address ||
        header->partition_size != partition->size ||
        file_size != sizeof(*header) + partition->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t nvs_backup_file_size(FILE *file, size_t *file_size)
{
    if (fseek(file, 0, SEEK_END) != 0) {
        return ESP_FAIL;
    }
    const long end = ftell(file);
    if (end < 0 || (uint64_t)end > SIZE_MAX || fseek(file, 0, SEEK_SET) != 0) {
        return ESP_FAIL;
    }
    *file_size = (size_t)end;
    return ESP_OK;
}

static esp_err_t nvs_backup_validate_payload(
    FILE *file,
    const solar_os_nvs_backup_header_t *header)
{
    uint8_t buffer[NVS_BACKUP_IO_BUFFER_SIZE];
    uint32_t crc = 0xffffffffUL;
    size_t remaining = header->partition_size;
    while (remaining > 0U) {
        const size_t count = remaining < sizeof(buffer) ?
            remaining : sizeof(buffer);
        if (fread(buffer, 1U, count, file) != count) {
            return ESP_FAIL;
        }
        crc = nvs_backup_crc32_update(crc, buffer, count);
        remaining -= count;
    }
    return (~crc == header->payload_crc32) ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t solar_os_nvs_backup_create(
    const char *path,
    solar_os_nvs_backup_result_t *result)
{
    if (path == NULL || path[0] == '\0' || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    if (!nvs_backup_begin()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_partition_t *partition = nvs_backup_partition();
    const esp_err_t error = partition != NULL ?
        nvs_backup_write_snapshot(partition, path, result) : ESP_ERR_NOT_FOUND;
    nvs_backup_end();
    return error;
}

esp_err_t solar_os_nvs_backup_restore(
    const char *path,
    solar_os_nvs_backup_result_t *result)
{
    if (path == NULL || path[0] == '\0' || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    if (!nvs_backup_begin()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = ESP_OK;
    const esp_partition_t *partition = nvs_backup_partition();
    FILE *file = NULL;
    solar_os_nvs_backup_header_t header;
    size_t file_size = 0;
    if (partition == NULL) {
        error = ESP_ERR_NOT_FOUND;
    } else {
        file = fopen(path, "rb");
        if (file == NULL) {
            error = ESP_ERR_NOT_FOUND;
        }
    }
    if (error == ESP_OK) {
        error = nvs_backup_file_size(file, &file_size);
    }
    if (error == ESP_OK &&
        fread(&header, sizeof(header), 1U, file) != 1U) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        error = nvs_backup_validate_header(&header, partition, file_size);
    }
    if (error == ESP_OK) {
        error = nvs_backup_validate_payload(file, &header);
    }
    if (error == ESP_OK && fseek(file, sizeof(header), SEEK_SET) != 0) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        error = nvs_flash_deinit();
        if (error == ESP_OK) {
            result->reboot_required = true;
        }
    }
    if (error == ESP_OK) {
        error = esp_partition_erase_range(partition, 0, partition->size);
    }

    uint8_t buffer[NVS_BACKUP_IO_BUFFER_SIZE];
    uint32_t written_crc = 0xffffffffUL;
    for (size_t offset = 0;
         error == ESP_OK && offset < partition->size;) {
        const size_t remaining = partition->size - offset;
        const size_t count = remaining < sizeof(buffer) ?
            remaining : sizeof(buffer);
        if (fread(buffer, 1U, count, file) != count) {
            error = ESP_FAIL;
            break;
        }
        written_crc = nvs_backup_crc32_update(written_crc, buffer, count);
        error = esp_partition_write(partition, offset, buffer, count);
        offset += count;
    }
    if (error == ESP_OK && ~written_crc != header.payload_crc32) {
        error = ESP_ERR_INVALID_CRC;
    }

    uint32_t verify_crc = 0xffffffffUL;
    for (size_t offset = 0;
         error == ESP_OK && offset < partition->size;) {
        const size_t remaining = partition->size - offset;
        const size_t count = remaining < sizeof(buffer) ?
            remaining : sizeof(buffer);
        error = esp_partition_read(partition, offset, buffer, count);
        if (error == ESP_OK) {
            verify_crc = nvs_backup_crc32_update(verify_crc, buffer, count);
            offset += count;
        }
    }
    if (error == ESP_OK && ~verify_crc != header.payload_crc32) {
        error = ESP_ERR_INVALID_CRC;
    }

    if (file != NULL) {
        fclose(file);
    }
    if (error == ESP_OK) {
        result->partition_size = partition->size;
        result->file_size = file_size;
        result->crc32 = header.payload_crc32;
    }
    nvs_backup_end();
    return error;
}
