#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_LOG_CAPACITY 256
#define SOLAR_OS_LOG_TAG_MAX 24
#define SOLAR_OS_LOG_MESSAGE_MAX 160

typedef enum {
    SOLAR_OS_LOG_LEVEL_ERROR = 0,
    SOLAR_OS_LOG_LEVEL_WARN = 1,
    SOLAR_OS_LOG_LEVEL_INFO = 2,
    SOLAR_OS_LOG_LEVEL_DEBUG = 3,
} solar_os_log_level_t;

typedef struct {
    uint32_t sequence;
    uint32_t timestamp_ms;
    solar_os_log_level_t level;
    bool truncated;
    char tag[SOLAR_OS_LOG_TAG_MAX];
    char message[SOLAR_OS_LOG_MESSAGE_MAX];
} solar_os_log_entry_t;

typedef struct {
    bool initialized;
    bool cdc_enabled;
    bool ring_in_psram;
    solar_os_log_level_t level;
    size_t capacity;
    size_t count;
    size_t bytes;
    uint32_t dropped;
} solar_os_log_status_t;

esp_err_t solar_os_log_init(void);
esp_err_t solar_os_log_clear(void);
esp_err_t solar_os_log_set_level(solar_os_log_level_t level);
esp_err_t solar_os_log_set_cdc_enabled(bool enabled);
esp_err_t solar_os_log_get_status(solar_os_log_status_t *status);
size_t solar_os_log_snapshot(solar_os_log_entry_t *entries,
                             size_t max_entries,
                             size_t *total_entries);
size_t solar_os_log_snapshot_since(uint32_t after_sequence,
                                   solar_os_log_level_t threshold,
                                   solar_os_log_entry_t *entries,
                                   size_t max_entries,
                                   size_t *total_entries);
esp_err_t solar_os_log_write(solar_os_log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
esp_err_t solar_os_log_vwrite(solar_os_log_level_t level,
                              const char *tag,
                              const char *fmt,
                              va_list args);
esp_err_t solar_os_log_buffer_hex(solar_os_log_level_t level,
                                  const char *tag,
                                  const void *data,
                                  size_t len);
const char *solar_os_log_level_name(solar_os_log_level_t level);
bool solar_os_log_parse_level(const char *text, solar_os_log_level_t *level);

#define SOLAR_OS_LOGE(tag, fmt, ...) \
    (void)solar_os_log_write(SOLAR_OS_LOG_LEVEL_ERROR, (tag), (fmt), ##__VA_ARGS__)
#define SOLAR_OS_LOGW(tag, fmt, ...) \
    (void)solar_os_log_write(SOLAR_OS_LOG_LEVEL_WARN, (tag), (fmt), ##__VA_ARGS__)
#define SOLAR_OS_LOGI(tag, fmt, ...) \
    (void)solar_os_log_write(SOLAR_OS_LOG_LEVEL_INFO, (tag), (fmt), ##__VA_ARGS__)
#define SOLAR_OS_LOGD(tag, fmt, ...) \
    (void)solar_os_log_write(SOLAR_OS_LOG_LEVEL_DEBUG, (tag), (fmt), ##__VA_ARGS__)
