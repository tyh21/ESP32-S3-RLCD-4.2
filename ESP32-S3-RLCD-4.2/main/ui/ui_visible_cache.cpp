// 实现可见页面天气整点和每日文字跨日缓存过期规则。
#include "ui_visible_cache.h"

#include "sensor_time.h"

namespace {
constexpr int kSecondsPerMinute = 60;
constexpr int kMinutesPerHour = 60;
constexpr int kHoursPerDay = 24;
constexpr int kSecondsPerHour = kSecondsPerMinute * kMinutesPerHour;
constexpr int kSecondsPerDay = kSecondsPerHour * kHoursPerDay;

static_assert(kSecondsPerHour == 60 * 60, "visible weather fallback must be one hour");
static_assert(kSecondsPerDay == 24 * 60 * 60, "daily saying fallback must be one day");
} // namespace

bool ui_weather_cache_stale(time_t now_value, time_t last_sync_time)
{
    if (last_sync_time <= 0) {
        return true;
    }
    struct tm now_local = {};
    struct tm last_local = {};
    localtime_r(&now_value, &now_local);
    localtime_r(&last_sync_time, &last_local);
    if (!is_tm_plausible(now_local) || !is_tm_plausible(last_local)) {
        return now_value - last_sync_time >= kSecondsPerHour;
    }
    return now_local.tm_year != last_local.tm_year ||
           now_local.tm_yday != last_local.tm_yday ||
           now_local.tm_hour != last_local.tm_hour;
}

bool ui_daily_saying_cache_stale(const struct tm &local_value,
                                 time_t now_value,
                                 bool snapshot_ready,
                                 time_t last_sync_time)
{
    if (!snapshot_ready || last_sync_time <= 0) {
        return true;
    }
    struct tm saying_local = {};
    localtime_r(&last_sync_time, &saying_local);
    if (!is_tm_plausible(saying_local) || !is_tm_plausible(local_value)) {
        return now_value - last_sync_time >= kSecondsPerDay;
    }
    return saying_local.tm_year != local_value.tm_year ||
           saying_local.tm_yday != local_value.tm_yday;
}
