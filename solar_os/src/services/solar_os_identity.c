#include "solar_os_identity.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "solar_os_log.h"
#include "solar_os_storage.h"

#define SOLAR_OS_IDENTITY_NVS_NAMESPACE "identity"
#define SOLAR_OS_IDENTITY_USER_KEY "user"
#define SOLAR_OS_IDENTITY_HOSTNAME_KEY "hostname"
#define SOLAR_OS_IDENTITY_DIR ".solar"
#define SOLAR_OS_IDENTITY_USER_FILE "user"
#define SOLAR_OS_IDENTITY_HOSTNAME_FILE "hostname"

static const char *TAG = "identity";
static portMUX_TYPE identity_lock = portMUX_INITIALIZER_UNLOCKED;
static bool identity_initialized;
static char identity_user[SOLAR_OS_IDENTITY_USER_MAX] =
    SOLAR_OS_IDENTITY_DEFAULT_USER;
static char identity_hostname[SOLAR_OS_IDENTITY_HOSTNAME_MAX] =
    SOLAR_OS_IDENTITY_DEFAULT_HOSTNAME;

static bool identity_char_is_valid(char ch)
{
    const unsigned char value = (unsigned char)ch;
    return isalnum(value) || ch == '-' || ch == '_' || ch == '.';
}

static bool identity_value_is_valid(const char *value, size_t max_len)
{
    if (value == NULL || value[0] == '\0' || strlen(value) >= max_len) {
        return false;
    }

    for (const char *p = value; *p != '\0'; p++) {
        if (!identity_char_is_valid(*p)) {
            return false;
        }
    }
    return true;
}

static void identity_trim(char *value)
{
    if (value == NULL) {
        return;
    }

    size_t len = strlen(value);
    while (len > 0 && isspace((unsigned char)value[len - 1])) {
        value[--len] = '\0';
    }

    char *start = value;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != value) {
        memmove(value, start, strlen(start) + 1);
    }
}

static bool identity_read_legacy_file(const char *name,
                                      char *buffer,
                                      size_t len)
{
    if (buffer == NULL || len == 0) {
        return false;
    }

    if (!solar_os_storage_is_mounted()) {
        return false;
    }

    char dir[SOLAR_OS_STORAGE_PATH_MAX];
    char path[SOLAR_OS_STORAGE_PATH_MAX];
    if (solar_os_storage_default_path(SOLAR_OS_IDENTITY_DIR, dir, sizeof(dir)) != ESP_OK ||
        solar_os_storage_join_path(dir, name, path, sizeof(path)) != ESP_OK) {
        return false;
    }

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }

    bool loaded = false;
    char value[64];
    if (fgets(value, sizeof(value), file) != NULL) {
        identity_trim(value);
        if (identity_value_is_valid(value, len)) {
            strlcpy(buffer, value, len);
            loaded = true;
        }
    }
    fclose(file);
    return loaded;
}

static bool identity_read_nvs(nvs_handle_t nvs,
                              const char *key,
                              char *buffer,
                              size_t len)
{
    size_t stored_len = len;
    return nvs_get_str(nvs, key, buffer, &stored_len) == ESP_OK &&
        identity_value_is_valid(buffer, len);
}

static esp_err_t identity_write_nvs(nvs_handle_t nvs,
                                    const char *key,
                                    const char *value)
{
    esp_err_t ret = nvs_set_str(nvs, key, value);
    return ret == ESP_OK ? nvs_commit(nvs) : ret;
}

esp_err_t solar_os_identity_init(void)
{
    portENTER_CRITICAL(&identity_lock);
    const bool already_initialized = identity_initialized;
    portEXIT_CRITICAL(&identity_lock);
    if (already_initialized) {
        return ESP_OK;
    }

    char user[SOLAR_OS_IDENTITY_USER_MAX] = SOLAR_OS_IDENTITY_DEFAULT_USER;
    char hostname[SOLAR_OS_IDENTITY_HOSTNAME_MAX] =
        SOLAR_OS_IDENTITY_DEFAULT_HOSTNAME;
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(SOLAR_OS_IDENTITY_NVS_NAMESPACE,
                             NVS_READWRITE,
                             &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    const bool user_in_nvs =
        identity_read_nvs(nvs, SOLAR_OS_IDENTITY_USER_KEY, user, sizeof(user));
    const bool hostname_in_nvs =
        identity_read_nvs(nvs,
                          SOLAR_OS_IDENTITY_HOSTNAME_KEY,
                          hostname,
                          sizeof(hostname));
    bool migrated = false;
    if (!user_in_nvs &&
        identity_read_legacy_file(SOLAR_OS_IDENTITY_USER_FILE,
                                  user,
                                  sizeof(user))) {
        ret = nvs_set_str(nvs, SOLAR_OS_IDENTITY_USER_KEY, user);
        migrated = ret == ESP_OK;
    }
    if (ret == ESP_OK &&
        !hostname_in_nvs &&
        identity_read_legacy_file(SOLAR_OS_IDENTITY_HOSTNAME_FILE,
                                  hostname,
                                  sizeof(hostname))) {
        ret = nvs_set_str(nvs, SOLAR_OS_IDENTITY_HOSTNAME_KEY, hostname);
        migrated = ret == ESP_OK || migrated;
    }
    if (ret == ESP_OK && migrated) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    portENTER_CRITICAL(&identity_lock);
    strlcpy(identity_user, user, sizeof(identity_user));
    strlcpy(identity_hostname, hostname, sizeof(identity_hostname));
    identity_initialized = true;
    portEXIT_CRITICAL(&identity_lock);

    if (migrated) {
        SOLAR_OS_LOGI(TAG, "migrated legacy .solar identity to NVS");
    }
    return ESP_OK;
}

void solar_os_identity_get_user(char *buffer, size_t len)
{
    if (buffer == NULL || len == 0) {
        return;
    }
    (void)solar_os_identity_init();
    portENTER_CRITICAL(&identity_lock);
    strlcpy(buffer, identity_user, len);
    portEXIT_CRITICAL(&identity_lock);
}

void solar_os_identity_get_hostname(char *buffer, size_t len)
{
    if (buffer == NULL || len == 0) {
        return;
    }
    (void)solar_os_identity_init();
    portENTER_CRITICAL(&identity_lock);
    strlcpy(buffer, identity_hostname, len);
    portEXIT_CRITICAL(&identity_lock);
}

static esp_err_t identity_set_value(const char *key,
                                    const char *value,
                                    size_t max_len,
                                    char *cached,
                                    size_t cached_len)
{
    if (!identity_value_is_valid(value, max_len)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = solar_os_identity_init();
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_handle_t nvs;
    ret = nvs_open(SOLAR_OS_IDENTITY_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret == ESP_OK) {
        ret = identity_write_nvs(nvs, key, value);
        nvs_close(nvs);
    }
    if (ret != ESP_OK) {
        return ret;
    }

    portENTER_CRITICAL(&identity_lock);
    strlcpy(cached, value, cached_len);
    portEXIT_CRITICAL(&identity_lock);
    return ESP_OK;
}

esp_err_t solar_os_identity_set_user(const char *user)
{
    return identity_set_value(SOLAR_OS_IDENTITY_USER_KEY,
                              user,
                              SOLAR_OS_IDENTITY_USER_MAX,
                              identity_user,
                              sizeof(identity_user));
}

esp_err_t solar_os_identity_set_hostname(const char *hostname)
{
    return identity_set_value(SOLAR_OS_IDENTITY_HOSTNAME_KEY,
                              hostname,
                              SOLAR_OS_IDENTITY_HOSTNAME_MAX,
                              identity_hostname,
                              sizeof(identity_hostname));
}

void solar_os_identity_format(char *buffer, size_t len)
{
    if (buffer == NULL || len == 0) {
        return;
    }

    char user[SOLAR_OS_IDENTITY_USER_MAX];
    char hostname[SOLAR_OS_IDENTITY_HOSTNAME_MAX];
    solar_os_identity_get_user(user, sizeof(user));
    solar_os_identity_get_hostname(hostname, sizeof(hostname));
    snprintf(buffer, len, "%s@%s", user, hostname);
}
