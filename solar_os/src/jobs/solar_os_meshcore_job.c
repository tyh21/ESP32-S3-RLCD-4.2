#include "solar_os_meshcore_job.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_jobs.h"
#include "solar_os_log.h"
#include "solar_os_task.h"

#define MESHCORE_LOOP_DELAY_MS 5U
#define MESHCORE_STOP_WAIT_MS 4000U

static const char *TAG = "meshcore";

typedef struct {
    volatile bool stop_requested;
    volatile bool task_done;
    TaskHandle_t task;
} meshcore_job_state_t;

static meshcore_job_state_t meshcore_job;

static bool parse_args(int argc,
                       char **argv,
                       const char **radio,
                       const char **profile)
{
    int first = 0;
    if (argc > 0 && argv != NULL && argv[0] != NULL &&
        strcmp(argv[0], solar_os_meshcore_job.name) == 0) {
        first = 1;
    }
    if (argv == NULL || argc - first != 2) {
        return false;
    }
    *radio = argv[first];
    *profile = argv[first + 1];
    return true;
}

static void meshcore_task(void *arg)
{
    (void)arg;
    while (!meshcore_job.stop_requested) {
        solar_os_meshcore_loop_once();
        solar_os_meshcore_note_stack_watermark(
            (uint32_t)uxTaskGetStackHighWaterMark(NULL) *
            sizeof(StackType_t));
        vTaskDelay(pdMS_TO_TICKS(MESHCORE_LOOP_DELAY_MS));
    }
    meshcore_job.task_done = true;
    meshcore_job.task = NULL;
    solar_os_task_delete_internal(NULL);
}

static esp_err_t meshcore_start(solar_os_context_t *ctx,
                                int argc,
                                char **argv)
{
    (void)ctx;
    const char *radio = NULL;
    const char *profile = NULL;
    if (!parse_args(argc, argv, &radio, &profile)) {
        return ESP_ERR_INVALID_ARG;
    }

    char owner[SOLAR_OS_JOB_OWNER_MAX];
    esp_err_t error = solar_os_jobs_owner_name(
        solar_os_meshcore_job.name, owner, sizeof(owner));
    if (error != ESP_OK) {
        return error;
    }
    error = solar_os_meshcore_start(radio, profile, owner);
    if (error != ESP_OK) {
        return error;
    }

    memset(&meshcore_job, 0, sizeof(meshcore_job));
    if (solar_os_task_create_pinned_internal(
            meshcore_task,
            "meshcore",
            SOLAR_OS_MESHCORE_WORKER_STACK,
            NULL,
            tskIDLE_PRIORITY + 2,
            &meshcore_job.task,
            tskNO_AFFINITY,
            SOLAR_OS_TASK_ROLE_BACKGROUND) != pdPASS) {
        solar_os_meshcore_stop();
        return ESP_ERR_NO_MEM;
    }
    (void)solar_os_jobs_note_resource(
        solar_os_meshcore_job.name,
        SOLAR_OS_JOB_RESOURCE_CUSTOM,
        radio,
        "MeshCore packet radio");
    SOLAR_OS_LOGI(TAG, "started radio=%s profile=%s", radio, profile);
    return ESP_OK;
}

static void meshcore_stop(solar_os_context_t *ctx)
{
    (void)ctx;
    meshcore_job.stop_requested = true;
    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(MESHCORE_STOP_WAIT_MS);
    while (!meshcore_job.task_done && meshcore_job.task != NULL &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(1);
    }
    if (meshcore_job.task != NULL) {
        solar_os_task_delete_internal(meshcore_job.task);
        meshcore_job.task = NULL;
    }
    solar_os_meshcore_stop();
    SOLAR_OS_LOGI(TAG, "stopped");
}

void solar_os_meshcore_job_get_status(solar_os_meshcore_status_t *status)
{
    if (status != NULL) {
        (void)solar_os_meshcore_get_status(status);
    }
}

const solar_os_job_t solar_os_meshcore_job = {
    .name = "meshcore",
    .summary = "MeshCore secure radio messaging",
    .start = meshcore_start,
    .stop = meshcore_stop,
    .worker_stack_bytes = SOLAR_OS_MESHCORE_WORKER_STACK,
};
