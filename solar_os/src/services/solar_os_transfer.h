#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    SOLAR_OS_TRANSFER_PROTOCOL_RAW,
    SOLAR_OS_TRANSFER_PROTOCOL_ZMODEM,
    SOLAR_OS_TRANSFER_PROTOCOL_KERMIT,
} solar_os_transfer_protocol_t;

typedef struct {
    uint64_t bytes;
    bool cancelled;
    bool idle_timeout;
} solar_os_transfer_result_t;

typedef bool (*solar_os_transfer_cancel_cb_t)(void *user);
typedef void (*solar_os_transfer_progress_cb_t)(uint64_t bytes, void *user);

typedef struct {
    const char *port_name;
    const char *path;
    solar_os_transfer_protocol_t protocol;
    uint32_t char_delay_ms;
    uint32_t idle_timeout_ms;
    bool append;
    solar_os_transfer_cancel_cb_t should_cancel;
    solar_os_transfer_progress_cb_t progress;
    void *user;
} solar_os_transfer_options_t;

const char *solar_os_transfer_protocol_name(solar_os_transfer_protocol_t protocol);
bool solar_os_transfer_parse_protocol(const char *text, solar_os_transfer_protocol_t *protocol);
esp_err_t solar_os_transfer_send(const solar_os_transfer_options_t *options,
                                 solar_os_transfer_result_t *result);
esp_err_t solar_os_transfer_recv(const solar_os_transfer_options_t *options,
                                 solar_os_transfer_result_t *result);
