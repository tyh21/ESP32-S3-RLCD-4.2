#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_STREAM_MAX 40
#define SOLAR_OS_STREAM_ID_MAX 24
#define SOLAR_OS_STREAM_PROVIDER_MAX 20
#define SOLAR_OS_STREAM_DEVICE_MAX 16
#define SOLAR_OS_STREAM_UNIT_MAX 16
#define SOLAR_OS_STREAM_FORMAT_MAX 24
#define SOLAR_OS_STREAM_SUMMARY_MAX 64
#define SOLAR_OS_STREAM_OWNER_MAX 24
#define SOLAR_OS_STREAM_CSV_HEADER_MAX 96
#define SOLAR_OS_STREAM_CSV_LINE_MAX 256
#define SOLAR_OS_STREAM_CHANGE_KEY_MAX 96

typedef enum {
    SOLAR_OS_STREAM_TYPE_SCALAR,
    SOLAR_OS_STREAM_TYPE_EVENT,
    SOLAR_OS_STREAM_TYPE_BYTES,
    SOLAR_OS_STREAM_TYPE_AUDIO,
} solar_os_stream_type_t;

typedef enum {
    SOLAR_OS_STREAM_DIRECTION_SOURCE,
    SOLAR_OS_STREAM_DIRECTION_SINK,
    SOLAR_OS_STREAM_DIRECTION_DUPLEX,
} solar_os_stream_direction_t;

typedef enum {
    SOLAR_OS_STREAM_SHARING_SHARED,
    SOLAR_OS_STREAM_SHARING_EXCLUSIVE,
    SOLAR_OS_STREAM_SHARING_FANOUT,
    SOLAR_OS_STREAM_SHARING_MIXED,
} solar_os_stream_sharing_t;

typedef enum {
    SOLAR_OS_STREAM_AUDIO_S16_LE,
} solar_os_stream_audio_sample_format_t;

typedef struct {
    solar_os_stream_audio_sample_format_t sample_format;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint16_t frames_per_block;
} solar_os_stream_audio_format_t;

typedef struct {
    char id[SOLAR_OS_STREAM_ID_MAX];
    char provider[SOLAR_OS_STREAM_PROVIDER_MAX];
    char device[SOLAR_OS_STREAM_DEVICE_MAX];
    solar_os_stream_type_t type;
    solar_os_stream_direction_t direction;
    solar_os_stream_sharing_t sharing;
    char unit[SOLAR_OS_STREAM_UNIT_MAX];
    char format[SOLAR_OS_STREAM_FORMAT_MAX];
    char summary[SOLAR_OS_STREAM_SUMMARY_MAX];
    solar_os_stream_audio_format_t audio;
    uint32_t active_handles;
    char owner[SOLAR_OS_STREAM_OWNER_MAX];
    uint64_t read_units;
    uint64_t written_units;
    uint32_t overruns;
    uint32_t underruns;
} solar_os_stream_info_t;

typedef struct {
    uint32_t window_ms;
    uint32_t timeout_ms;
} solar_os_stream_read_options_t;

typedef struct {
    bool has_data;
    bool time_valid;
    uint64_t time_ms;
    uint64_t uptime_ms;
    char line[SOLAR_OS_STREAM_CSV_LINE_MAX];
    char change_key[SOLAR_OS_STREAM_CHANGE_KEY_MAX];
} solar_os_stream_csv_record_t;

typedef struct solar_os_stream_handle {
    char id[SOLAR_OS_STREAM_ID_MAX];
    solar_os_stream_type_t type;
    solar_os_stream_direction_t direction;
    int slot;
    uint32_t generation;
    void *context;
    uintptr_t private_data[2];
    solar_os_stream_audio_format_t audio;
} solar_os_stream_handle_t;

#define SOLAR_OS_STREAM_HANDLE_INIT { \
    .id = "", \
    .type = SOLAR_OS_STREAM_TYPE_SCALAR, \
    .direction = SOLAR_OS_STREAM_DIRECTION_SOURCE, \
    .slot = -1, \
    .generation = 0, \
    .context = NULL, \
    .private_data = {0}, \
    .audio = {0}, \
}

typedef struct {
    solar_os_stream_direction_t direction;
    uint32_t timeout_ms;
    solar_os_stream_audio_format_t requested_audio;
} solar_os_stream_open_options_t;

typedef esp_err_t (*solar_os_stream_open_fn)(
    void *user,
    const char *owner,
    const solar_os_stream_open_options_t *options,
    solar_os_stream_handle_t *handle);
typedef void (*solar_os_stream_close_fn)(void *user,
                                         solar_os_stream_handle_t *handle);
typedef esp_err_t (*solar_os_stream_read_fn)(void *user,
                                             solar_os_stream_handle_t *handle,
                                             void *data,
                                             size_t len,
                                             uint32_t timeout_ms,
                                             size_t *read_len);
typedef esp_err_t (*solar_os_stream_write_fn)(void *user,
                                              solar_os_stream_handle_t *handle,
                                              const void *data,
                                              size_t len,
                                              uint32_t timeout_ms,
                                              size_t *written);
typedef esp_err_t (*solar_os_stream_read_scalar_fn)(
    void *user,
    const solar_os_stream_read_options_t *options,
    float *value);
typedef esp_err_t (*solar_os_stream_csv_header_fn)(void *user,
                                                   char *header,
                                                   size_t header_len);
typedef esp_err_t (*solar_os_stream_read_csv_fn)(
    void *user,
    solar_os_stream_handle_t *handle,
    const solar_os_stream_read_options_t *options,
    solar_os_stream_csv_record_t *record);

typedef struct {
    solar_os_stream_info_t info;
    solar_os_stream_open_fn open;
    solar_os_stream_close_fn close;
    solar_os_stream_read_fn read;
    solar_os_stream_write_fn write;
    solar_os_stream_read_scalar_fn read_scalar;
    /* Optional compatibility adapters. New consumers use typed reads. */
    solar_os_stream_csv_header_fn csv_header;
    solar_os_stream_read_csv_fn read_csv;
    void *user;
} solar_os_stream_driver_t;

esp_err_t solar_os_stream_init(void);
esp_err_t solar_os_stream_register(const solar_os_stream_driver_t *driver);
esp_err_t solar_os_stream_unregister(const char *id);
size_t solar_os_stream_count(void);
bool solar_os_stream_get(size_t index, solar_os_stream_info_t *info);
esp_err_t solar_os_stream_get_info(const char *id, solar_os_stream_info_t *info);
const char *solar_os_stream_type_name(solar_os_stream_type_t type);
const char *solar_os_stream_direction_name(solar_os_stream_direction_t direction);
const char *solar_os_stream_sharing_name(solar_os_stream_sharing_t sharing);
const char *solar_os_stream_audio_sample_format_name(
    solar_os_stream_audio_sample_format_t format);

esp_err_t solar_os_stream_open_ex(const char *id,
                                  const char *owner,
                                  const solar_os_stream_open_options_t *options,
                                  solar_os_stream_handle_t *handle);
esp_err_t solar_os_stream_open(const char *id,
                               const char *owner,
                               solar_os_stream_handle_t *handle);
void solar_os_stream_close(solar_os_stream_handle_t *handle);
bool solar_os_stream_handle_valid(const solar_os_stream_handle_t *handle);
esp_err_t solar_os_stream_read(solar_os_stream_handle_t *handle,
                               void *data,
                               size_t len,
                               uint32_t timeout_ms,
                               size_t *read_len);
esp_err_t solar_os_stream_write(solar_os_stream_handle_t *handle,
                                const void *data,
                                size_t len,
                                uint32_t timeout_ms,
                                size_t *written);

esp_err_t solar_os_stream_csv_header(const solar_os_stream_info_t *info,
                                     char *header,
                                     size_t header_len);
esp_err_t solar_os_stream_read_csv(solar_os_stream_handle_t *handle,
                                   const solar_os_stream_read_options_t *options,
                                   solar_os_stream_csv_record_t *record);
esp_err_t solar_os_stream_read_scalar(
    solar_os_stream_handle_t *handle,
    const solar_os_stream_read_options_t *options,
    float *value);
