#include "solar_os_controls_job.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_config.h"
#include "solar_os_controls.h"
#include "solar_os_jobs.h"
#if SOLAR_OS_PACKAGE_SERVICE_MIDI
#include "solar_os_midi.h"
#endif
#include "solar_os_parameters.h"
#include "solar_os_shell_io.h"
#include "solar_os_stream.h"
#include "solar_os_task.h"

#define CONTROLS_JOB_TICK_MS 20U
#define CONTROLS_PICKUP_TOLERANCE 512U
#define CONTROLS_WORKER_STACK 4096U
#define CONTROLS_WORKER_PRIORITY (tskIDLE_PRIORITY + 1)
#define CONTROLS_NOTIFY_SAMPLE (1U << 0)
#define CONTROLS_NOTIFY_STOP (1U << 1)

typedef struct {
    bool running;
    uint32_t ticks;
    uint32_t samples;
    uint32_t binding_updates;
    uint32_t sample_errors;
    uint32_t binding_errors;
    uint32_t sample_ms;
    TaskHandle_t worker_task;
    volatile bool worker_done;
} controls_job_state_t;

static controls_job_state_t controls_job;

static void controls_job_worker(void *arg);

static esp_err_t controls_job_start(solar_os_context_t *ctx,
                                    int argc,
                                    char **argv)
{
    (void)argv;
    if (argc != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    controls_job = (controls_job_state_t) {
        .running = true,
    };
    if (solar_os_task_create_pinned_internal(
            controls_job_worker,
            "controls_worker",
            CONTROLS_WORKER_STACK,
            NULL,
            CONTROLS_WORKER_PRIORITY,
            &controls_job.worker_task,
            tskNO_AFFINITY,
            SOLAR_OS_TASK_ROLE_BACKGROUND) != pdPASS) {
        controls_job.running = false;
        return ESP_ERR_NO_MEM;
    }
    solar_os_shell_io_t *io = solar_os_context_shell_io(ctx);
    if (io != NULL) {
        solar_os_shell_io_printf(io,
                                 "controls job started: %u controls, %u bindings\n",
                                 (unsigned)solar_os_control_count(),
                                 (unsigned)solar_os_control_binding_count());
    }
    (void)solar_os_jobs_note_resource("controls",
                                      SOLAR_OS_JOB_RESOURCE_CUSTOM,
                                      "controls",
                                      "control mapper");
    return ESP_OK;
}

static void controls_job_stop(solar_os_context_t *ctx)
{
    if (!controls_job.running && controls_job.worker_task == NULL) {
        return;
    }
    controls_job.running = false;
    if (controls_job.worker_task != NULL) {
        (void)xTaskNotify(controls_job.worker_task,
                          CONTROLS_NOTIFY_STOP,
                          eSetBits);
    }
    if (!solar_os_task_wait_done(controls_job.worker_task,
                                 &controls_job.worker_done,
                                 SOLAR_OS_TASK_STOP_WAIT_MS)) {
        return;
    }

    solar_os_shell_io_t *io = solar_os_context_shell_io(ctx);
    if (io != NULL) {
        solar_os_shell_io_printf(
            io,
            "controls job stopped: %u samples, %u bindings, %u errors\n",
            (unsigned)controls_job.samples,
            (unsigned)controls_job.binding_updates,
            (unsigned)(controls_job.sample_errors +
                       controls_job.binding_errors));
    }
    controls_job.worker_task = NULL;
    controls_job.worker_done = false;
}

static void controls_job_sample(uint32_t now_ms)
{
    const size_t count = solar_os_control_count();
    for (size_t i = 0; i < count; i++) {
        solar_os_control_info_t control;
        if (!solar_os_control_get_info(i, &control) ||
            control.config.source[0] == '\0') {
            continue;
        }
        solar_os_stream_handle_t stream =
            (solar_os_stream_handle_t)SOLAR_OS_STREAM_HANDLE_INIT;
        esp_err_t err = solar_os_stream_open(control.config.source,
                                             "controls",
                                             &stream);
        float value = 0.0f;
        if (err == ESP_OK) {
            const solar_os_stream_read_options_t options = {
                .window_ms = CONTROLS_JOB_TICK_MS,
            };
            err = solar_os_stream_read_scalar(&stream, &options, &value);
            solar_os_stream_close(&stream);
        }
        if (err == ESP_OK) {
            err = solar_os_control_publish_sample(control.config.name,
                                                  value,
                                                  now_ms);
        }
        if (err == ESP_OK) {
            controls_job.samples++;
        } else {
            solar_os_control_note_read_error(control.config.name, err);
            controls_job.sample_errors++;
        }
    }
}

static bool controls_crossed(uint16_t previous,
                             uint16_t current,
                             uint16_t target)
{
    const uint16_t lower = previous < current ? previous : current;
    const uint16_t upper = previous < current ? current : previous;
    const uint16_t distance = current > target ? current - target : target - current;
    return (target >= lower && target <= upper) ||
           distance <= CONTROLS_PICKUP_TOLERANCE;
}

static void controls_job_apply_parameter(
    const solar_os_control_info_t *control,
    const solar_os_control_binding_info_t *binding)
{
    uint16_t target_value = 0U;
    esp_err_t err = solar_os_parameter_get_normalized(binding->parameter,
                                                      &target_value);
    if (err != ESP_OK) {
        solar_os_control_binding_note(binding->id,
                                      binding->last_generation,
                                      binding->pickup_seen,
                                      binding->pickup_latched,
                                      binding->pickup_previous,
                                      binding->last_target_value,
                                      err,
                                      false);
        return;
    }
    if (binding->last_generation == control->generation &&
        binding->last_error == ESP_OK) {
        if (!binding->pickup) {
            return;
        }
        const uint16_t distance = binding->last_target_value > target_value ?
            binding->last_target_value - target_value :
            target_value - binding->last_target_value;
        if (binding->pickup_latched) {
            if (distance > CONTROLS_PICKUP_TOLERANCE) {
                solar_os_control_binding_note(binding->id,
                                              binding->last_generation,
                                              false,
                                              false,
                                              control->normalized,
                                              target_value,
                                              ESP_OK,
                                              false);
            }
            return;
        }
    }

    bool pickup_seen = binding->pickup_seen;
    bool pickup_latched = binding->pickup_latched;
    uint16_t pickup_previous = binding->pickup_previous;
    if (binding->pickup && !pickup_latched) {
        if (!pickup_seen) {
            pickup_seen = true;
            pickup_previous = control->normalized;
            solar_os_control_binding_note(binding->id,
                                          binding->last_generation,
                                          pickup_seen,
                                          false,
                                          pickup_previous,
                                          target_value,
                                          ESP_OK,
                                          false);
            return;
        }
        if (!controls_crossed(pickup_previous,
                              control->normalized,
                              target_value)) {
            solar_os_control_binding_note(binding->id,
                                          binding->last_generation,
                                          true,
                                          false,
                                          control->normalized,
                                          target_value,
                                          ESP_OK,
                                          false);
            return;
        }
        pickup_latched = true;
    }

    err = solar_os_parameter_set_normalized(binding->parameter,
                                            control->normalized);
    uint16_t applied_target = control->normalized;
    if (err == ESP_OK) {
        (void)solar_os_parameter_get_normalized(binding->parameter,
                                                &applied_target);
    }
    solar_os_control_binding_note(binding->id,
                                  control->generation,
                                  pickup_seen,
                                  pickup_latched,
                                  control->normalized,
                                  applied_target,
                                  err,
                                  err == ESP_OK);
    if (err == ESP_OK) {
        controls_job.binding_updates++;
    } else if (err != ESP_ERR_NOT_FOUND && err != ESP_ERR_INVALID_STATE) {
        controls_job.binding_errors++;
    }
}

static void controls_job_apply_midi(
    const solar_os_control_info_t *control,
    const solar_os_control_binding_info_t *binding)
{
    if (binding->last_generation == control->generation &&
        binding->last_error == ESP_OK) {
        return;
    }
#if SOLAR_OS_PACKAGE_SERVICE_MIDI
    const solar_os_midi_message_t message = {
        .status = (uint8_t)(0xb0U | (binding->midi_channel - 1U)),
        .data1 = binding->midi_controller,
        .data2 = (uint8_t)(((uint32_t)control->normalized * 127U + 32767U) /
                          SOLAR_OS_CONTROL_NORMALIZED_MAX),
        .length = 3U,
    };
    const esp_err_t err = solar_os_midi_send(&message);
#else
    const esp_err_t err = ESP_ERR_NOT_SUPPORTED;
#endif
    solar_os_control_binding_note(binding->id,
                                  control->generation,
                                  false,
                                  true,
                                  control->normalized,
                                  control->normalized,
                                  err,
                                  err == ESP_OK);
    if (err == ESP_OK) {
        controls_job.binding_updates++;
    } else if (err != ESP_ERR_INVALID_STATE) {
        controls_job.binding_errors++;
    }
}

static void controls_job_apply_bindings(void)
{
    const size_t count = solar_os_control_binding_count();
    for (size_t i = 0; i < count; i++) {
        solar_os_control_binding_info_t binding;
        solar_os_control_info_t control;
        if (!solar_os_control_binding_get(i, &binding) ||
            solar_os_control_find(binding.control, &control) != ESP_OK ||
            !control.has_value) {
            continue;
        }
        if (binding.target == SOLAR_OS_CONTROL_TARGET_MIDI_CC) {
            controls_job_apply_midi(&control, &binding);
        } else {
            controls_job_apply_parameter(&control, &binding);
        }
    }
}

static void controls_job_worker(void *arg)
{
    (void)arg;

    for (;;) {
        uint32_t notification = 0U;
        (void)xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);
        if ((notification & CONTROLS_NOTIFY_STOP) != 0U) {
            break;
        }
        if ((notification & CONTROLS_NOTIFY_SAMPLE) != 0U &&
            controls_job.running) {
            controls_job_sample(controls_job.sample_ms);
        }
    }

    controls_job.worker_done = true;
    solar_os_task_delete_internal(NULL);
}

static bool controls_job_event(solar_os_context_t *ctx,
                               const solar_os_event_t *event)
{
    (void)ctx;
    if (!controls_job.running || event == NULL ||
        event->type != SOLAR_OS_EVENT_TICK) {
        return false;
    }
    controls_job.ticks++;
    controls_job.sample_ms = event->data.tick_ms;
    if (controls_job.worker_task == NULL ||
        xTaskNotify(controls_job.worker_task,
                    CONTROLS_NOTIFY_SAMPLE,
                    eSetBits) != pdPASS) {
        controls_job.sample_errors++;
    }
    controls_job_apply_bindings();
    return false;
}

const solar_os_job_t solar_os_controls_job = {
    .name = "controls",
    .summary = "map scalar streams to native parameters and MIDI controls",
    .kind = SOLAR_OS_JOB_KIND_BACKGROUND,
    .start = controls_job_start,
    .stop = controls_job_stop,
    .event = controls_job_event,
    .worker_stack_bytes = CONTROLS_WORKER_STACK,
    .tick_interval_ms = CONTROLS_JOB_TICK_MS,
    .tick_deadline_ms = 2U,
};
