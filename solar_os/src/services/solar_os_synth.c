#include "solar_os_synth.h"

#include <inttypes.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "solar_os_audio.h"
#include "solar_os_log.h"
#include "solar_os_stream.h"
#include "solar_os_task.h"

#define SYNTH_TASK_STACK 5120U
#define SYNTH_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)
#define SYNTH_START_WAIT_MS 2000U

typedef struct {
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t started;
    TaskHandle_t task;
    solar_os_synth_render_cb_t render;
    void *user;
    char owner[SOLAR_OS_SYNTH_OWNER_MAX];
    char requested_playback_stream[SOLAR_OS_STREAM_ID_MAX];
    char playback_stream[SOLAR_OS_STREAM_ID_MAX];
    size_t block_frames;
    uint32_t sample_rate;
    volatile bool stop_requested;
    volatile bool task_done;
    bool starting;
    bool running;
    esp_err_t start_result;
    esp_err_t last_error;
    uint64_t rendered_frames;
    uint32_t rendered_blocks;
    uint32_t render_deadline_misses;
    uint32_t write_errors;
    uint32_t max_render_us;
    int16_t samples[SOLAR_OS_SYNTH_BLOCK_FRAMES_MAX * 2U];
} synth_state_t;

static const char *TAG = "solar_os_synth";
static synth_state_t synth;
static StaticSemaphore_t synth_mutex_storage;
static StaticSemaphore_t synth_started_storage;
static portMUX_TYPE synth_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t synth_ensure_sync(void)
{
    portENTER_CRITICAL(&synth_init_lock);
    if (synth.mutex == NULL) {
        synth.mutex = xSemaphoreCreateMutexStatic(&synth_mutex_storage);
    }
    if (synth.started == NULL) {
        synth.started = xSemaphoreCreateBinaryStatic(&synth_started_storage);
    }
    const bool ready = synth.mutex != NULL && synth.started != NULL;
    portEXIT_CRITICAL(&synth_init_lock);
    return ready ? ESP_OK : ESP_ERR_NO_MEM;
}

static void synth_lock(void)
{
    (void)xSemaphoreTake(synth.mutex, portMAX_DELAY);
}

static void synth_unlock(void)
{
    (void)xSemaphoreGive(synth.mutex);
}

static void synth_finish(esp_err_t result)
{
    synth_lock();
    synth.running = false;
    synth.starting = false;
    synth.last_error = result;
    synth.task_done = true;
    synth.render = NULL;
    synth.user = NULL;
    synth.owner[0] = '\0';
    synth.requested_playback_stream[0] = '\0';
    synth.playback_stream[0] = '\0';
    synth_unlock();
}

static void synth_worker(void *arg)
{
    (void)arg;
    solar_os_stream_handle_t stream =
        (solar_os_stream_handle_t)SOLAR_OS_STREAM_HANDLE_INIT;

    synth_lock();
    char owner[SOLAR_OS_SYNTH_OWNER_MAX];
    strlcpy(owner, synth.owner, sizeof(owner));
    char playback_stream[SOLAR_OS_STREAM_ID_MAX];
    strlcpy(playback_stream, synth.requested_playback_stream,
            sizeof(playback_stream));
    const size_t block_frames = synth.block_frames;
    solar_os_synth_render_cb_t render = synth.render;
    void *user = synth.user;
    synth_unlock();

    const solar_os_stream_open_options_t open_options = {
        .direction = SOLAR_OS_STREAM_DIRECTION_SINK,
        .timeout_ms = 0U,
        .requested_audio = {
            .sample_format = SOLAR_OS_STREAM_AUDIO_S16_LE,
            .channels = 2U,
            .bits_per_sample = 16U,
        },
    };
    esp_err_t result =
        playback_stream[0] != '\0'
            ? solar_os_stream_open_ex(playback_stream, owner, &open_options,
                                      &stream)
            : solar_os_audio_open_default(SOLAR_OS_STREAM_DIRECTION_SINK, owner,
                                          &open_options, &stream, NULL);
    const solar_os_stream_audio_format_t format = stream.audio;
    if (result == ESP_OK &&
        (format.channels != 2U || format.bits_per_sample != 16U)) {
        solar_os_stream_close(&stream);
        result = ESP_ERR_NOT_SUPPORTED;
    }

    synth_lock();
    synth.start_result = result;
    synth.starting = false;
    synth.running = result == ESP_OK;
    synth.sample_rate = result == ESP_OK ? format.sample_rate : 0U;
    strlcpy(synth.playback_stream,
            result == ESP_OK ? stream.id : playback_stream,
            sizeof(synth.playback_stream));
    synth.last_error = result;
    synth_unlock();
    (void)xSemaphoreGive(synth.started);

    if (result != ESP_OK) {
        SOLAR_OS_LOGW(TAG, "start failed: owner=%s err=%s", owner,
                      esp_err_to_name(result));
        synth_finish(result);
        solar_os_task_delete_internal(NULL);
        return;
    }

    const uint32_t block_deadline_us =
        (uint32_t)(((uint64_t)block_frames * 1000000ULL) / format.sample_rate);
    SOLAR_OS_LOGI(TAG, "started: owner=%s output=%s rate=%" PRIu32 " frames=%u",
                  owner, stream.id, format.sample_rate, (unsigned)block_frames);

    while (!synth.stop_requested) {
        memset(synth.samples, 0,
               block_frames * format.channels * sizeof(synth.samples[0]));
        const int64_t render_started = esp_timer_get_time();
        render(synth.samples, block_frames, format.sample_rate, user);
        const uint32_t render_us =
            (uint32_t)(esp_timer_get_time() - render_started);

        synth_lock();
        synth.rendered_frames += block_frames;
        synth.rendered_blocks++;
        if (render_us > synth.max_render_us) {
            synth.max_render_us = render_us;
        }
        if (render_us > block_deadline_us) {
            synth.render_deadline_misses++;
        }
        synth_unlock();

        const size_t bytes =
            block_frames * format.channels * sizeof(synth.samples[0]);
        size_t written = 0U;
        result = solar_os_stream_write(
            &stream, synth.samples, bytes, UINT32_MAX, &written);
        if (result != ESP_OK || written != bytes) {
            if (result == ESP_OK) {
                result = ESP_ERR_INVALID_SIZE;
            }
            synth_lock();
            synth.write_errors++;
            synth_unlock();
            break;
        }
    }

    if (solar_os_stream_handle_valid(&stream)) {
        memset(synth.samples, 0,
               block_frames * format.channels * sizeof(synth.samples[0]));
        size_t written = 0U;
        (void)solar_os_stream_write(
            &stream, synth.samples,
            block_frames * format.channels * sizeof(synth.samples[0]),
            UINT32_MAX, &written);
        solar_os_stream_close(&stream);
    }

    solar_os_synth_status_t status;
    solar_os_synth_get_status(&status);
    SOLAR_OS_LOGI(TAG,
                  "stopped: owner=%s blocks=%" PRIu32
                  " misses=%" PRIu32 " errors=%" PRIu32,
                  owner, status.rendered_blocks,
                  status.render_deadline_misses, status.write_errors);
    synth_finish(result);
    solar_os_task_delete_internal(NULL);
}

esp_err_t solar_os_synth_start(const solar_os_synth_config_t *config)
{
    if (config == NULL || config->owner == NULL || config->owner[0] == '\0' ||
        config->render == NULL ||
        (config->playback_stream != NULL &&
         strnlen(config->playback_stream, SOLAR_OS_STREAM_ID_MAX) >=
             SOLAR_OS_STREAM_ID_MAX) ||
        config->block_frames < SOLAR_OS_SYNTH_BLOCK_FRAMES_MIN ||
        config->block_frames > SOLAR_OS_SYNTH_BLOCK_FRAMES_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = synth_ensure_sync();
    if (result != ESP_OK) {
        return result;
    }

    synth_lock();
    TaskHandle_t finished_task =
        synth.task_done ? synth.task : NULL;
    synth_unlock();
    if (finished_task != NULL) {
        (void)solar_os_task_wait_done(finished_task, &synth.task_done,
                                      SOLAR_OS_TASK_STOP_WAIT_MS);
        synth_lock();
        if (synth.task == finished_task && synth.task_done) {
            synth.task = NULL;
        }
        synth_unlock();
    }

    synth_lock();
    if (synth.task != NULL || synth.starting || synth.running) {
        synth_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    while (xSemaphoreTake(synth.started, 0) == pdTRUE) {
    }
    synth.render = config->render;
    synth.user = config->user;
    strlcpy(synth.owner, config->owner, sizeof(synth.owner));
    strlcpy(synth.requested_playback_stream,
            config->playback_stream != NULL ? config->playback_stream : "",
            sizeof(synth.requested_playback_stream));
    synth.playback_stream[0] = '\0';
    synth.block_frames = config->block_frames;
    synth.sample_rate = 0;
    synth.stop_requested = false;
    synth.task_done = false;
    synth.starting = true;
    synth.running = false;
    synth.start_result = ESP_ERR_INVALID_STATE;
    synth.last_error = ESP_OK;
    synth.rendered_frames = 0;
    synth.rendered_blocks = 0;
    synth.render_deadline_misses = 0;
    synth.write_errors = 0;
    synth.max_render_us = 0;

    const BaseType_t created = solar_os_task_create_pinned_internal(
        synth_worker, "synth", SYNTH_TASK_STACK, NULL, SYNTH_TASK_PRIORITY,
        &synth.task, tskNO_AFFINITY, SOLAR_OS_TASK_ROLE_SYSTEM);
    if (created != pdPASS) {
        synth.task = NULL;
        synth.starting = false;
        synth.last_error = ESP_ERR_NO_MEM;
        synth_unlock();
        return ESP_ERR_NO_MEM;
    }
    synth_unlock();

    TickType_t wait = pdMS_TO_TICKS(SYNTH_START_WAIT_MS);
    if (wait == 0) {
        wait = 1;
    }
    if (xSemaphoreTake(synth.started, wait) != pdTRUE) {
        (void)solar_os_synth_stop(config->owner);
        return ESP_ERR_TIMEOUT;
    }
    synth_lock();
    result = synth.start_result;
    synth_unlock();
    return result;
}

esp_err_t solar_os_synth_stop(const char *owner)
{
    if (synth_ensure_sync() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    synth_lock();
    if (synth.task == NULL) {
        synth.starting = false;
        synth.running = false;
        synth_unlock();
        return ESP_OK;
    }
    if (owner != NULL && strcmp(owner, synth.owner) != 0 &&
        !synth.task_done) {
        synth_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    TaskHandle_t task = synth.task;
    if (task == xTaskGetCurrentTaskHandle()) {
        synth_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    synth.stop_requested = true;
    synth_unlock();

    if (!solar_os_task_wait_done(task, &synth.task_done,
                                 SOLAR_OS_TASK_STOP_WAIT_MS)) {
        SOLAR_OS_LOGW(TAG, "stop timeout: owner=%s",
                      owner != NULL ? owner : "*");
        return ESP_ERR_TIMEOUT;
    }
    synth_lock();
    if (synth.task == task) {
        synth.task = NULL;
    }
    synth_unlock();
    return ESP_OK;
}

void solar_os_synth_get_status(solar_os_synth_status_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    if (synth_ensure_sync() != ESP_OK) {
        status->last_error = ESP_ERR_NO_MEM;
        return;
    }
    synth_lock();
    status->starting = synth.starting;
    status->running = synth.running;
    strlcpy(status->owner, synth.owner, sizeof(status->owner));
    strlcpy(status->playback_stream, synth.playback_stream,
            sizeof(status->playback_stream));
    status->sample_rate = synth.sample_rate;
    status->block_frames = synth.block_frames;
    status->rendered_frames = synth.rendered_frames;
    status->rendered_blocks = synth.rendered_blocks;
    status->render_deadline_misses = synth.render_deadline_misses;
    status->write_errors = synth.write_errors;
    status->max_render_us = synth.max_render_us;
    status->last_error = synth.last_error;
    synth_unlock();
}
