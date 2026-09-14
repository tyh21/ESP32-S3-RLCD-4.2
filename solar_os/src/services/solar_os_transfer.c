#include "solar_os_transfer.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_port.h"
#include "solar_os_zmodem.h"

#define TRANSFER_CHUNK_SIZE 512U
#define TRANSFER_IDLE_READ_TIMEOUT_MS 100U
#define TRANSFER_DELAY_CANCEL_POLL_MS 25U
#define TRANSFER_PROGRESS_STEP_BYTES (16U * 1024U)

typedef enum {
    TRANSFER_DIRECTION_SEND,
    TRANSFER_DIRECTION_RECV,
} transfer_direction_t;

const char *solar_os_transfer_protocol_name(solar_os_transfer_protocol_t protocol)
{
    switch (protocol) {
    case SOLAR_OS_TRANSFER_PROTOCOL_RAW:
        return "raw";
    case SOLAR_OS_TRANSFER_PROTOCOL_ZMODEM:
        return "zmodem";
    case SOLAR_OS_TRANSFER_PROTOCOL_KERMIT:
        return "kermit";
    default:
        return "unknown";
    }
}

bool solar_os_transfer_parse_protocol(const char *text, solar_os_transfer_protocol_t *protocol)
{
    if (text == NULL || protocol == NULL) {
        return false;
    }
    if (strcmp(text, "raw") == 0) {
        *protocol = SOLAR_OS_TRANSFER_PROTOCOL_RAW;
        return true;
    }
    if (strcmp(text, "zmodem") == 0 || strcmp(text, "rz") == 0 || strcmp(text, "sz") == 0) {
        *protocol = SOLAR_OS_TRANSFER_PROTOCOL_ZMODEM;
        return true;
    }
    if (strcmp(text, "kermit") == 0) {
        *protocol = SOLAR_OS_TRANSFER_PROTOCOL_KERMIT;
        return true;
    }
    return false;
}

static bool transfer_should_cancel(const solar_os_transfer_options_t *options)
{
    return options != NULL &&
        options->should_cancel != NULL &&
        options->should_cancel(options->user);
}

static void transfer_report_progress(const solar_os_transfer_options_t *options,
                                     uint64_t bytes,
                                     uint64_t *next_progress)
{
    if (options == NULL || options->progress == NULL || next_progress == NULL) {
        return;
    }
    if (bytes < *next_progress) {
        return;
    }

    options->progress(bytes, options->user);
    *next_progress = bytes + TRANSFER_PROGRESS_STEP_BYTES;
}

static bool transfer_delay_or_cancel(const solar_os_transfer_options_t *options, uint32_t delay_ms)
{
    uint32_t remaining_ms = delay_ms;

    while (remaining_ms > 0) {
        if (transfer_should_cancel(options)) {
            return true;
        }
        const uint32_t step_ms =
            remaining_ms > TRANSFER_DELAY_CANCEL_POLL_MS ?
                TRANSFER_DELAY_CANCEL_POLL_MS :
                remaining_ms;
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        remaining_ms -= step_ms;
    }

    return transfer_should_cancel(options);
}

static esp_err_t transfer_claim_port(const solar_os_transfer_options_t *options,
                                     transfer_direction_t direction,
                                     solar_os_port_handle_t *port)
{
    if (options == NULL ||
        options->port_name == NULL ||
        options->path == NULL ||
        port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    solar_os_port_info_t info;
    esp_err_t err = solar_os_port_get_info(options->port_name, &info);
    if (err != ESP_OK) {
        return err;
    }

    const uint32_t required = options->protocol == SOLAR_OS_TRANSFER_PROTOCOL_RAW ?
        (direction == TRANSFER_DIRECTION_SEND ? SOLAR_OS_PORT_CAP_WRITE : SOLAR_OS_PORT_CAP_READ) :
        (SOLAR_OS_PORT_CAP_READ | SOLAR_OS_PORT_CAP_WRITE);
    if ((info.capabilities & required) == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return solar_os_port_claim(options->port_name, "xfer", port);
}

static esp_err_t transfer_write_all(const solar_os_transfer_options_t *options,
                                    const solar_os_port_handle_t *port,
                                    const uint8_t *data,
                                    size_t len,
                                    size_t *written_total,
                                    bool *cancelled)
{
    size_t offset = 0;

    if (written_total != NULL) {
        *written_total = 0;
    }
    if (cancelled != NULL) {
        *cancelled = false;
    }

    while (offset < len) {
        if (transfer_should_cancel(options)) {
            if (cancelled != NULL) {
                *cancelled = true;
            }
            return ESP_OK;
        }

        if (options->char_delay_ms == 0) {
            size_t written = 0;
            const esp_err_t err = solar_os_port_write(port, &data[offset], len - offset, &written);
            if (written > 0) {
                offset += written;
                if (written_total != NULL) {
                    *written_total = offset;
                }
            }
            if (err != ESP_OK) {
                return err;
            }
            if (written == 0) {
                return ESP_FAIL;
            }
            continue;
        }

        size_t written = 0;
        const esp_err_t err = solar_os_port_write(port, &data[offset], 1, &written);
        if (written > 0) {
            offset += written;
            if (written_total != NULL) {
                *written_total = offset;
            }
        }
        if (err != ESP_OK) {
            return err;
        }
        if (written != 1) {
            return ESP_FAIL;
        }
        if (offset < len && transfer_delay_or_cancel(options, options->char_delay_ms)) {
            if (cancelled != NULL) {
                *cancelled = true;
            }
            return ESP_OK;
        }
    }

    return ESP_OK;
}

esp_err_t solar_os_transfer_send(const solar_os_transfer_options_t *options,
                                 solar_os_transfer_result_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (options == NULL || options->port_name == NULL || options->path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (options->protocol == SOLAR_OS_TRANSFER_PROTOCOL_KERMIT) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    FILE *file = fopen(options->path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    solar_os_port_handle_t port = SOLAR_OS_PORT_HANDLE_INIT;
    esp_err_t err = transfer_claim_port(options, TRANSFER_DIRECTION_SEND, &port);
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    if (options->protocol == SOLAR_OS_TRANSFER_PROTOCOL_ZMODEM) {
        struct stat st;
        if (stat(options->path, &st) != 0) {
            (void)solar_os_port_release(&port);
            fclose(file);
            return ESP_FAIL;
        }

        err = solar_os_zmodem_send(options, &port, file, (uint64_t)st.st_size, result);
        const esp_err_t release_err = solar_os_port_release(&port);
        if (fclose(file) != 0 && err == ESP_OK) {
            err = ESP_FAIL;
        }
        if (err == ESP_OK) {
            err = release_err;
        }
        return err;
    }

    uint8_t buffer[TRANSFER_CHUNK_SIZE];
    uint64_t transferred = 0;
    uint64_t next_progress = TRANSFER_PROGRESS_STEP_BYTES;
    bool cancelled = false;
    while (!cancelled) {
        const size_t read_len = fread(buffer, 1, sizeof(buffer), file);
        if (read_len > 0) {
            size_t written = 0;
            bool chunk_cancelled = false;
            err = transfer_write_all(options, &port, buffer, read_len, &written, &chunk_cancelled);
            transferred += written;
            transfer_report_progress(options, transferred, &next_progress);
            if (err != ESP_OK) {
                break;
            }
            if (chunk_cancelled) {
                cancelled = true;
                break;
            }
            if (written != read_len) {
                err = ESP_FAIL;
                break;
            }
        }

        if (read_len < sizeof(buffer)) {
            if (ferror(file)) {
                err = ESP_FAIL;
            }
            break;
        }
    }

    if (fclose(file) != 0 && err == ESP_OK) {
        err = ESP_FAIL;
    }
    const esp_err_t release_err = solar_os_port_release(&port);
    if (err == ESP_OK) {
        err = release_err;
    }

    if (result != NULL) {
        result->bytes = transferred;
        result->cancelled = cancelled;
    }
    return err;
}

esp_err_t solar_os_transfer_recv(const solar_os_transfer_options_t *options,
                                 solar_os_transfer_result_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }

    if (options == NULL || options->port_name == NULL || options->path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (options->protocol == SOLAR_OS_TRANSFER_PROTOCOL_KERMIT) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    solar_os_port_handle_t port = SOLAR_OS_PORT_HANDLE_INIT;
    esp_err_t err = transfer_claim_port(options, TRANSFER_DIRECTION_RECV, &port);
    if (err != ESP_OK) {
        return err;
    }

    if (options->protocol == SOLAR_OS_TRANSFER_PROTOCOL_ZMODEM) {
        err = solar_os_zmodem_recv(options, &port, result);
        const esp_err_t release_err = solar_os_port_release(&port);
        if (err == ESP_OK) {
            err = release_err;
        }
        return err;
    }

    FILE *file = fopen(options->path, options->append ? "ab" : "wb");
    if (file == NULL) {
        (void)solar_os_port_release(&port);
        return ESP_FAIL;
    }

    uint8_t buffer[TRANSFER_CHUNK_SIZE];
    uint64_t transferred = 0;
    uint64_t next_progress = TRANSFER_PROGRESS_STEP_BYTES;
    bool cancelled = false;
    bool idle_timeout = false;
    int64_t last_activity_us = esp_timer_get_time();

    while (!cancelled && !idle_timeout) {
        if (transfer_should_cancel(options)) {
            cancelled = true;
            break;
        }

        size_t read_len = 0;
        err = solar_os_port_read(&port,
                                 buffer,
                                 sizeof(buffer),
                                 TRANSFER_IDLE_READ_TIMEOUT_MS,
                                 &read_len);
        if (err == ESP_ERR_TIMEOUT || (err == ESP_OK && read_len == 0)) {
            err = ESP_OK;
            if (options->idle_timeout_ms != 0) {
                const int64_t idle_us = esp_timer_get_time() - last_activity_us;
                if (idle_us >= (int64_t)options->idle_timeout_ms * 1000) {
                    idle_timeout = true;
                }
            }
            continue;
        }
        if (err != ESP_OK) {
            break;
        }

        if (fwrite(buffer, 1, read_len, file) != read_len ||
            fflush(file) != 0) {
            err = ESP_FAIL;
            break;
        }
        transferred += read_len;
        last_activity_us = esp_timer_get_time();
        transfer_report_progress(options, transferred, &next_progress);
    }

    if (fclose(file) != 0 && err == ESP_OK) {
        err = ESP_FAIL;
    }
    const esp_err_t release_err = solar_os_port_release(&port);
    if (err == ESP_OK) {
        err = release_err;
    }

    if (result != NULL) {
        result->bytes = transferred;
        result->cancelled = cancelled;
        result->idle_timeout = idle_timeout;
    }
    return err;
}
