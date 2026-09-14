#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_partition.h"
#include "solar_os_nvs_backup.h"

#define TEST_PARTITION_SIZE 4096U
#define TEST_BACKUP_HEADER_SIZE 44U

static esp_partition_t test_partition = {
    .address = 0x9000U,
    .size = TEST_PARTITION_SIZE,
    .label = "nvs",
};
static uint8_t test_partition_data[TEST_PARTITION_SIZE];
static bool test_nvs_initialized = true;
static bool test_nvs_init_fails;

const esp_partition_t *esp_partition_find_first(uint8_t type,
                                                uint8_t subtype,
                                                const char *label)
{
    return type == ESP_PARTITION_TYPE_DATA &&
        subtype == ESP_PARTITION_SUBTYPE_DATA_NVS &&
        strcmp(label, test_partition.label) == 0 ? &test_partition : NULL;
}

esp_err_t esp_partition_read(const esp_partition_t *partition,
                             size_t offset,
                             void *destination,
                             size_t size)
{
    if (partition != &test_partition || destination == NULL ||
        offset > partition->size || size > partition->size - offset) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(destination, test_partition_data + offset, size);
    return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *partition,
                              size_t offset,
                              const void *source,
                              size_t size)
{
    if (test_nvs_initialized || partition != &test_partition ||
        source == NULL || offset > partition->size ||
        size > partition->size - offset) {
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(test_partition_data + offset, source, size);
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *partition,
                                    size_t offset,
                                    size_t size)
{
    if (test_nvs_initialized || partition != &test_partition ||
        offset > partition->size || size > partition->size - offset) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(test_partition_data + offset, 0xff, size);
    return ESP_OK;
}

esp_err_t nvs_flash_init(void)
{
    if (test_nvs_init_fails) {
        return ESP_FAIL;
    }
    test_nvs_initialized = true;
    return ESP_OK;
}

esp_err_t nvs_flash_deinit(void)
{
    if (!test_nvs_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    test_nvs_initialized = false;
    return ESP_OK;
}

static void fill_partition(uint8_t seed)
{
    for (size_t i = 0; i < sizeof(test_partition_data); i++) {
        test_partition_data[i] = (uint8_t)(seed + i * 17U);
    }
}

static void corrupt_backup(const char *path)
{
    FILE *file = fopen(path, "r+b");
    assert(file != NULL);
    assert(fseek(file, TEST_BACKUP_HEADER_SIZE + 123U, SEEK_SET) == 0);
    const int value = fgetc(file);
    assert(value != EOF);
    assert(fseek(file, -1L, SEEK_CUR) == 0);
    assert(fputc(value ^ 0x5a, file) != EOF);
    assert(fclose(file) == 0);
}

int main(void)
{
    char directory[] = "/tmp/solar-os-nvs-backup-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char path[256];
    assert(snprintf(path, sizeof(path), "%s/nvs.bin", directory) > 0);

    fill_partition(3U);
    uint8_t original[TEST_PARTITION_SIZE];
    memcpy(original, test_partition_data, sizeof(original));

    solar_os_nvs_backup_result_t result;
    assert(solar_os_nvs_backup_create(path, &result) == ESP_OK);
    assert(test_nvs_initialized);
    assert(!result.reboot_required);
    assert(result.partition_size == TEST_PARTITION_SIZE);
    assert(result.file_size == TEST_BACKUP_HEADER_SIZE + TEST_PARTITION_SIZE);
    struct stat status;
    assert(stat(path, &status) == 0);
    assert((size_t)status.st_size == result.file_size);

    fill_partition(91U);
    assert(memcmp(test_partition_data, original, sizeof(original)) != 0);
    assert(solar_os_nvs_backup_restore(path, &result) == ESP_OK);
    assert(result.reboot_required);
    assert(!test_nvs_initialized);
    assert(memcmp(test_partition_data, original, sizeof(original)) == 0);

    test_nvs_initialized = true;
    assert(solar_os_nvs_backup_create(path, &result) == ESP_OK);
    uint8_t before_invalid_restore[TEST_PARTITION_SIZE];
    memcpy(before_invalid_restore,
           test_partition_data,
           sizeof(before_invalid_restore));
    corrupt_backup(path);
    assert(solar_os_nvs_backup_restore(path, &result) == ESP_ERR_INVALID_CRC);
    assert(!result.reboot_required);
    assert(test_nvs_initialized);
    assert(memcmp(test_partition_data,
                  before_invalid_restore,
                  sizeof(before_invalid_restore)) == 0);

    assert(solar_os_nvs_backup_create(path, &result) == ESP_OK);
    test_partition.address += 0x1000U;
    assert(solar_os_nvs_backup_restore(path, &result) == ESP_ERR_INVALID_SIZE);
    assert(!result.reboot_required);
    assert(test_nvs_initialized);
    test_partition.address -= 0x1000U;

    test_nvs_init_fails = true;
    assert(solar_os_nvs_backup_create(path, &result) == ESP_FAIL);
    assert(result.reboot_required);
    test_nvs_init_fails = false;
    test_nvs_initialized = true;

    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    puts("nvs backup tests: ok");
    return 0;
}
