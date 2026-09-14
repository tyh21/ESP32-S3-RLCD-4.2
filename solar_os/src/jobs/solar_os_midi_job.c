#include "solar_os_midi_job.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "solar_os_buses.h"
#include "solar_os_jobs.h"
#include "solar_os_log.h"
#include "solar_os_midi.h"

#define MIDI_JOB_TICK_MS 2U
#define MIDI_JOB_RX_BUFFER_SIZE 64U

typedef struct {
    bool running;
    bool leased;
    char bus_name[SOLAR_OS_BUS_NAME_MAX];
    char owner[SOLAR_OS_JOB_OWNER_MAX];
    solar_os_midi_decoder_t decoder;
} midi_job_state_t;

static const char *TAG = "solar_os_midi";
static midi_job_state_t midi_job;

static void midi_job_cleanup(void)
{
    solar_os_midi_worker_stop();
    if (midi_job.leased) {
        (void)solar_os_bus_release(midi_job.bus_name,
                                   SOLAR_OS_BUS_PROTOCOL_MIDI,
                                   midi_job.owner);
    }
    memset(&midi_job, 0, sizeof(midi_job));
}

static esp_err_t midi_job_start(solar_os_context_t *ctx, int argc, char **argv)
{
    (void)ctx;
    if (argc != 2 || argv == NULL || argv[1] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&midi_job, 0, sizeof(midi_job));
    strlcpy(midi_job.bus_name, argv[1], sizeof(midi_job.bus_name));
    esp_err_t error = solar_os_jobs_owner_name("midi",
                                               midi_job.owner,
                                               sizeof(midi_job.owner));
    if (error != ESP_OK) {
        midi_job_cleanup();
        return error;
    }

    solar_os_bus_info_t info;
    if (!solar_os_bus_find(midi_job.bus_name, SOLAR_OS_BUS_PROTOCOL_MIDI, &info)) {
        midi_job_cleanup();
        return ESP_ERR_NOT_FOUND;
    }
    error = solar_os_bus_acquire(midi_job.bus_name,
                                 SOLAR_OS_BUS_PROTOCOL_MIDI,
                                 midi_job.owner);
    if (error != ESP_OK) {
        midi_job_cleanup();
        return error;
    }
    midi_job.leased = true;

    error = solar_os_midi_worker_start(midi_job.bus_name);
    if (error != ESP_OK) {
        midi_job_cleanup();
        return error;
    }
    solar_os_midi_decoder_reset(&midi_job.decoder);
    (void)solar_os_jobs_note_resource("midi",
                                      SOLAR_OS_JOB_RESOURCE_CUSTOM,
                                      midi_job.bus_name,
                                      "MIDI bus");
    midi_job.running = true;
    SOLAR_OS_LOGI(TAG,
                  "started on %s: TX GPIO%d RX GPIO%d baud=%u backend=uart%d",
                  midi_job.bus_name,
                  info.config.uart.tx_pin,
                  info.config.uart.rx_pin,
                  (unsigned)info.config.uart.baud_rate,
                  info.config.uart.port);
    return ESP_OK;
}

static void midi_job_stop(solar_os_context_t *ctx)
{
    (void)ctx;
    if (midi_job.running) {
        solar_os_midi_status_t status;
        solar_os_midi_get_status(&status);
        SOLAR_OS_LOGI(TAG,
                      "stopped: rx=%u/%u tx=%u/%u unsupported=%u drops=%u/%u",
                      (unsigned)status.rx_messages,
                      (unsigned)status.rx_bytes,
                      (unsigned)status.tx_messages,
                      (unsigned)status.tx_bytes,
                      (unsigned)status.parser_unsupported,
                      (unsigned)status.subscriber_drops,
                      (unsigned)status.tx_drops);
    }
    midi_job_cleanup();
}

static void midi_job_receive(void)
{
    uint8_t bytes[MIDI_JOB_RX_BUFFER_SIZE];
    size_t read_len = 0U;
    const esp_err_t error = solar_os_bus_midi_read(midi_job.bus_name,
                                                   bytes,
                                                   sizeof(bytes),
                                                   0U,
                                                   &read_len);
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
        solar_os_midi_worker_note_error(error);
        return;
    }
    if (read_len == 0U) {
        return;
    }
    solar_os_midi_worker_note_rx_bytes(read_len);
    for (size_t i = 0; i < read_len; i++) {
        solar_os_midi_message_t message;
        const solar_os_midi_decode_result_t result =
            solar_os_midi_decode_byte(&midi_job.decoder, bytes[i], &message);
        if (result == SOLAR_OS_MIDI_DECODE_MESSAGE) {
            solar_os_midi_worker_publish(&message);
        } else if (result == SOLAR_OS_MIDI_DECODE_UNSUPPORTED) {
            solar_os_midi_worker_note_unsupported();
        }
    }
}

static void midi_job_transmit(void)
{
    solar_os_midi_message_t message;
    while (solar_os_midi_worker_take_tx(&message)) {
        uint8_t bytes[3];
        const size_t length = solar_os_midi_encode(&message, bytes);
        if (length == 0U) {
            solar_os_midi_worker_note_error(ESP_ERR_INVALID_ARG);
            continue;
        }
        size_t written = 0U;
        const esp_err_t error = solar_os_bus_midi_write(midi_job.bus_name,
                                                        bytes,
                                                        length,
                                                        &written);
        if (error != ESP_OK || written != length) {
            solar_os_midi_worker_note_error(error != ESP_OK ? error : ESP_FAIL);
            break;
        }
        solar_os_midi_worker_note_tx(written);
    }
}

static bool midi_job_event(solar_os_context_t *ctx, const solar_os_event_t *event)
{
    (void)ctx;
    if (!midi_job.running || event == NULL || event->type != SOLAR_OS_EVENT_TICK) {
        return false;
    }
    midi_job_receive();
    midi_job_transmit();
    return false;
}

const solar_os_job_t solar_os_midi_job = {
    .name = "midi",
    .summary = "bidirectional MIDI transport on a named MIDI bus",
    .kind = SOLAR_OS_JOB_KIND_BACKGROUND,
    .start = midi_job_start,
    .stop = midi_job_stop,
    .event = midi_job_event,
    .tick_interval_ms = MIDI_JOB_TICK_MS,
    .tick_deadline_ms = 2U,
};
