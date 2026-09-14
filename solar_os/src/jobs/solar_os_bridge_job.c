#include "solar_os_bridge_job.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_jobs.h"
#include "solar_os_link.h"
#include "solar_os_log.h"
#include "solar_os_port.h"
#include "solar_os_task.h"

#define BRIDGE_JOB_TASK_STACK 4096
#define BRIDGE_JOB_TASK_PRIORITY (tskIDLE_PRIORITY + 3)
#define BRIDGE_JOB_BUFFER_SIZE 512
#define BRIDGE_JOB_READ_TIMEOUT_MS 10U

static const char *TAG = "solar_os_bridge";

typedef enum {
    BRIDGE_JOB_MODE_PORTS,
    BRIDGE_JOB_MODE_PORT_LINK,
} bridge_job_mode_t;

typedef struct {
    bool running;
    volatile bool stop_requested;
    bool terminal_failure;
    TaskHandle_t task;
    uint32_t generation;
    bridge_job_mode_t mode;
    solar_os_port_handle_t port_a;
    solar_os_port_handle_t port_b;
    char port_a_name[SOLAR_OS_PORT_NAME_MAX];
    char port_b_name[SOLAR_OS_PORT_NAME_MAX];
    char link_name[SOLAR_OS_LINK_NAME_MAX];
    uint32_t link_destination;
    size_t link_payload_mtu;
    uint32_t bytes_a_to_b;
    uint32_t bytes_b_to_a;
    uint32_t frames_a_to_b;
    uint32_t frames_b_to_a;
    uint32_t read_failures;
    uint32_t write_failures;
    esp_err_t last_error;
} bridge_job_state_t;

static bridge_job_state_t bridge_job = {
    .port_a = SOLAR_OS_PORT_HANDLE_INIT,
    .port_b = SOLAR_OS_PORT_HANDLE_INIT,
    .last_error = ESP_OK,
};

static esp_err_t bridge_job_validate_port(const char *name)
{
    solar_os_port_info_t info;

    const esp_err_t err = solar_os_port_get_info(name, &info);
    if (err != ESP_OK) {
        return err;
    }
    if (info.claimed) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((info.capabilities & (SOLAR_OS_PORT_CAP_READ | SOLAR_OS_PORT_CAP_WRITE)) !=
        (SOLAR_OS_PORT_CAP_READ | SOLAR_OS_PORT_CAP_WRITE)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static bool bridge_job_parse_destination(const char *text, uint32_t *destination)
{
    if (text == NULL || destination == NULL) {
        return false;
    }
    if (strcmp(text, "broadcast") == 0) {
        *destination = SOLAR_OS_LINK_BROADCAST;
        return true;
    }

    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0 ||
        parsed >= SOLAR_OS_LINK_BROADCAST) {
        return false;
    }
    *destination = (uint32_t)parsed;
    return true;
}

static esp_err_t bridge_job_validate_link(const char *name,
                                          size_t *payload_mtu)
{
    solar_os_link_status_t status;
    const esp_err_t err = solar_os_link_get_status(name, &status);
    if (err != ESP_OK) {
        return err;
    }
    if (status.frame_mtu <= SOLAR_OS_LINK_HEADER_SIZE + SOLAR_OS_LINK_CRC_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (payload_mtu != NULL) {
        *payload_mtu =
            status.frame_mtu - SOLAR_OS_LINK_HEADER_SIZE - SOLAR_OS_LINK_CRC_SIZE;
    }
    return ESP_OK;
}

static void bridge_job_cleanup(void)
{
    if (solar_os_port_handle_valid(&bridge_job.port_a)) {
        (void)solar_os_port_release(&bridge_job.port_a);
    }
    if (solar_os_port_handle_valid(&bridge_job.port_b)) {
        (void)solar_os_port_release(&bridge_job.port_b);
    }

    bridge_job.running = false;
    bridge_job.stop_requested = false;
    bridge_job.task = NULL;
    bridge_job.port_a_name[0] = '\0';
    bridge_job.port_b_name[0] = '\0';
    bridge_job.link_name[0] = '\0';
    bridge_job.link_destination = 0;
    bridge_job.link_payload_mtu = 0;
}

static esp_err_t bridge_job_write_all(const solar_os_port_handle_t *dst,
                                      const uint8_t *data,
                                      size_t len)
{
    size_t offset = 0;
    while (!bridge_job.stop_requested && offset < len) {
        size_t written = 0;
        const esp_err_t err = solar_os_port_write(dst, &data[offset], len - offset, &written);
        if (written > 0) {
            offset += written;
        }
        if (err != ESP_OK) {
            bridge_job.write_failures++;
            bridge_job.last_error = err;
            return err;
        }
        if (written == 0) {
            bridge_job.write_failures++;
            bridge_job.last_error = ESP_FAIL;
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static bool bridge_job_forward_once(const solar_os_port_handle_t *src,
                                    const solar_os_port_handle_t *dst,
                                    uint32_t *byte_counter,
                                    uint8_t *buffer,
                                    size_t buffer_len)
{
    size_t read_len = 0;
    const esp_err_t err = solar_os_port_read(src,
                                             buffer,
                                             buffer_len,
                                             BRIDGE_JOB_READ_TIMEOUT_MS,
                                             &read_len);
    if (err != ESP_OK) {
        if (err != ESP_ERR_TIMEOUT) {
            bridge_job.read_failures++;
            bridge_job.last_error = err;
        }
        return false;
    }
    if (read_len == 0) {
        return false;
    }

    const esp_err_t write_err = bridge_job_write_all(dst, buffer, read_len);
    if (write_err == ESP_OK && byte_counter != NULL) {
        *byte_counter += (uint32_t)read_len;
    }
    return true;
}

static bool bridge_job_forward_port_to_link(uint8_t *buffer,
                                            size_t buffer_len)
{
    const size_t read_max =
        bridge_job.link_payload_mtu < buffer_len ? bridge_job.link_payload_mtu : buffer_len;
    size_t read_len = 0;
    esp_err_t err = solar_os_port_read(&bridge_job.port_a,
                                       buffer,
                                       read_max,
                                       BRIDGE_JOB_READ_TIMEOUT_MS,
                                       &read_len);
    if (err != ESP_OK) {
        if (err != ESP_ERR_TIMEOUT) {
            bridge_job.read_failures++;
            bridge_job.last_error = err;
        }
        return false;
    }
    if (read_len == 0) {
        return false;
    }

    err = solar_os_link_send(bridge_job.link_name,
                             SOLAR_OS_LINK_MESSAGE_BINARY,
                             bridge_job.link_destination,
                             buffer,
                             read_len,
                             NULL);
    if (err == ESP_OK) {
        bridge_job.bytes_a_to_b += (uint32_t)read_len;
        bridge_job.frames_a_to_b++;
    } else {
        bridge_job.write_failures++;
        bridge_job.last_error = err;
        if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_STATE) {
            bridge_job.terminal_failure = true;
            bridge_job.stop_requested = true;
        }
    }
    return true;
}

static bool bridge_job_forward_link_to_port(void)
{
    solar_os_link_message_t message;
    const esp_err_t err =
        solar_os_link_receive(bridge_job.link_name, &message, 0);
    if (err == ESP_ERR_TIMEOUT) {
        return false;
    }
    if (err != ESP_OK) {
        bridge_job.read_failures++;
        bridge_job.last_error = err;
        if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_STATE) {
            bridge_job.terminal_failure = true;
            bridge_job.stop_requested = true;
        }
        return false;
    }

    if (message.payload_len == 0) {
        bridge_job.frames_b_to_a++;
        return true;
    }
    const esp_err_t write_err =
        bridge_job_write_all(&bridge_job.port_a, message.payload, message.payload_len);
    if (write_err == ESP_OK) {
        bridge_job.bytes_b_to_a += (uint32_t)message.payload_len;
        bridge_job.frames_b_to_a++;
    }
    return true;
}

static void bridge_job_task(void *arg)
{
    bridge_job_state_t *state = (bridge_job_state_t *)arg;
    uint8_t buffer[BRIDGE_JOB_BUFFER_SIZE];

    if (state->mode == BRIDGE_JOB_MODE_PORT_LINK) {
        SOLAR_OS_LOGI(TAG,
                      "started: %s <-> link %s destination=0x%08" PRIx32,
                      state->port_a_name,
                      state->link_name,
                      state->link_destination);
    } else {
        SOLAR_OS_LOGI(TAG,
                      "started: %s <-> %s",
                      state->port_a_name,
                      state->port_b_name);
    }

    while (!state->stop_requested) {
        bool moved_a = false;
        bool moved_b = false;
        if (state->mode == BRIDGE_JOB_MODE_PORT_LINK) {
            moved_a = bridge_job_forward_port_to_link(buffer, sizeof(buffer));
            moved_b = bridge_job_forward_link_to_port();
        } else {
            moved_a = bridge_job_forward_once(&state->port_a,
                                              &state->port_b,
                                              &state->bytes_a_to_b,
                                              buffer,
                                              sizeof(buffer));
            moved_b = bridge_job_forward_once(&state->port_b,
                                              &state->port_a,
                                              &state->bytes_b_to_a,
                                              buffer,
                                              sizeof(buffer));
        }
        if (!moved_a && !moved_b) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    const uint32_t generation = state->generation;
    const bool terminal_failure = state->terminal_failure;
    const esp_err_t last_error = state->last_error;
    if (state->mode == BRIDGE_JOB_MODE_PORT_LINK) {
        SOLAR_OS_LOGI(TAG,
                      "stopped: %s->%s=%" PRIu32 "/%" PRIu32
                      " %s->%s=%" PRIu32 "/%" PRIu32
                      " read_fail=%" PRIu32 " write_fail=%" PRIu32,
                      state->port_a_name,
                      state->link_name,
                      state->bytes_a_to_b,
                      state->frames_a_to_b,
                      state->link_name,
                      state->port_a_name,
                      state->bytes_b_to_a,
                      state->frames_b_to_a,
                      state->read_failures,
                      state->write_failures);
    } else {
        SOLAR_OS_LOGI(TAG,
                      "stopped: %s->%s=%" PRIu32 " %s->%s=%" PRIu32
                      " read_fail=%" PRIu32 " write_fail=%" PRIu32,
                      state->port_a_name,
                      state->port_b_name,
                      state->bytes_a_to_b,
                      state->port_b_name,
                      state->port_a_name,
                      state->bytes_b_to_a,
                      state->read_failures,
                      state->write_failures);
    }
    bridge_job_cleanup();
    if (terminal_failure) {
        (void)solar_os_jobs_mark_stopped(
            solar_os_bridge_job.name, generation, last_error);
    }
    solar_os_task_delete(NULL);
}

static esp_err_t bridge_job_start(solar_os_context_t *ctx, int argc, char **argv)
{
    (void)ctx;

    if ((argc != 3 && argc != 4) || argv == NULL ||
        argv[1] == NULL || argv[1][0] == '\0' ||
        argv[2] == NULL || argv[2][0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (bridge_job.running || bridge_job.task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    bridge_job_mode_t mode = BRIDGE_JOB_MODE_PORTS;
    size_t link_payload_mtu = 0;
    uint32_t link_destination = SOLAR_OS_LINK_BROADCAST;

    esp_err_t err = bridge_job_validate_port(argv[1]);
    if (err != ESP_OK) {
        return err;
    }

    solar_os_port_info_t second_port;
    if (solar_os_port_get_info(argv[2], &second_port) == ESP_OK) {
        if (argc != 3 || strcmp(argv[1], argv[2]) == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        err = bridge_job_validate_port(argv[2]);
        if (err != ESP_OK) {
            return err;
        }
    } else {
        mode = BRIDGE_JOB_MODE_PORT_LINK;
        err = bridge_job_validate_link(argv[2], &link_payload_mtu);
        if (err != ESP_OK) {
            return err;
        }
        if (argc == 4 &&
            !bridge_job_parse_destination(argv[3], &link_destination)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    err = solar_os_jobs_claim_port(solar_os_bridge_job.name, argv[1], &bridge_job.port_a);
    if (err != ESP_OK) {
        return err;
    }
    if (mode == BRIDGE_JOB_MODE_PORTS) {
        err = solar_os_jobs_claim_port(solar_os_bridge_job.name, argv[2], &bridge_job.port_b);
        if (err != ESP_OK) {
            (void)solar_os_port_release(&bridge_job.port_a);
            return err;
        }
    } else {
        char detail[SOLAR_OS_JOB_RESOURCE_DETAIL_MAX];
        if (link_destination == SOLAR_OS_LINK_BROADCAST) {
            strlcpy(detail, "Link broadcast", sizeof(detail));
        } else {
            snprintf(detail,
                     sizeof(detail),
                     "Link 0x%08" PRIx32,
                     link_destination);
        }
        (void)solar_os_jobs_note_resource(
            solar_os_bridge_job.name, SOLAR_OS_JOB_RESOURCE_CUSTOM, argv[2], detail);
    }

    bridge_job.running = true;
    bridge_job.stop_requested = false;
    bridge_job.terminal_failure = false;
    bridge_job.mode = mode;
    bridge_job.link_destination = link_destination;
    bridge_job.link_payload_mtu = link_payload_mtu;
    bridge_job.bytes_a_to_b = 0;
    bridge_job.bytes_b_to_a = 0;
    bridge_job.frames_a_to_b = 0;
    bridge_job.frames_b_to_a = 0;
    bridge_job.read_failures = 0;
    bridge_job.write_failures = 0;
    bridge_job.last_error = ESP_OK;
    strlcpy(bridge_job.port_a_name, argv[1], sizeof(bridge_job.port_a_name));
    if (mode == BRIDGE_JOB_MODE_PORTS) {
        strlcpy(bridge_job.port_b_name, argv[2], sizeof(bridge_job.port_b_name));
    } else {
        strlcpy(bridge_job.link_name, argv[2], sizeof(bridge_job.link_name));
    }

    err = solar_os_jobs_get_generation(solar_os_bridge_job.name, &bridge_job.generation);
    if (err != ESP_OK) {
        bridge_job_cleanup();
        return err;
    }

    if (solar_os_task_create_pinned(bridge_job_task,
                                    "bridge_job",
                                    BRIDGE_JOB_TASK_STACK,
                                    &bridge_job,
                                    BRIDGE_JOB_TASK_PRIORITY,
                                    &bridge_job.task,
                                    tskNO_AFFINITY,
                                    SOLAR_OS_TASK_ROLE_BACKGROUND) != pdPASS) {
        bridge_job_cleanup();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void bridge_job_stop(solar_os_context_t *ctx)
{
    (void)ctx;

    if (!bridge_job.running && bridge_job.task == NULL) {
        return;
    }

    bridge_job.stop_requested = true;
    if (bridge_job.task != NULL && bridge_job.task != xTaskGetCurrentTaskHandle()) {
        for (uint32_t i = 0; i < 80 && bridge_job.task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(25));
        }
    }
}

const solar_os_job_t solar_os_bridge_job = {
    .name = "bridge",
    .summary = "bidirectional port and Link bridge",
    .start = bridge_job_start,
    .stop = bridge_job_stop,
    .event = NULL,
    .worker_stack_bytes = BRIDGE_JOB_TASK_STACK,
};
