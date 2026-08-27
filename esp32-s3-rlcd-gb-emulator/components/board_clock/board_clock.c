#include "board_clock.h"

#include <sys/time.h>

#include "esp_log.h"

static const char *TAG = "board_clock";

static bool s_synced = false;
static int16_t s_timezone_offset_minutes = 0;

esp_err_t board_clock_sync_unix_ms(uint64_t unix_ms, int16_t timezone_offset_minutes)
{
    struct timeval tv = {
        .tv_sec = (time_t)(unix_ms / 1000ULL),
        .tv_usec = (suseconds_t)((unix_ms % 1000ULL) * 1000ULL),
    };

    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }

    s_timezone_offset_minutes = timezone_offset_minutes;
    s_synced = true;

    ESP_LOGI(TAG,
             "Time synced from phone: unix_ms=%llu timezone_offset_minutes=%d",
             (unsigned long long)unix_ms,
             (int)timezone_offset_minutes);
    return ESP_OK;
}

board_clock_status_t board_clock_get_status(void)
{
    board_clock_status_t status = {
        .unix_seconds = time(NULL),
        .timezone_offset_minutes = s_timezone_offset_minutes,
        .synced = s_synced,
    };
    return status;
}

time_t board_clock_get_phone_local_seconds(void)
{
    /*
     * JavaScript Date.getTimezoneOffset() 的含义是：
     *   UTC - local time，单位分钟。
     *
     * 例如中国时区 UTC+8 会返回 -480。
     * 所以 local = UTC - offset_minutes。
     */
    return time(NULL) - ((time_t)s_timezone_offset_minutes * 60);
}
