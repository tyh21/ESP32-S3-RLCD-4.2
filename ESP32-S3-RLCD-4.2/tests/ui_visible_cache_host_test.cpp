// 验证天气整点和每日文字跨日缓存的新鲜度与异常时间回退。
#include "ui_visible_cache.h"

#include <assert.h>
#include <stdlib.h>

namespace {
struct tm local_value(int year, int month, int day, int hour, int minute, int second)
{
    struct tm value = {};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    value.tm_isdst = -1;
    return value;
}

time_t local_epoch(int year, int month, int day, int hour, int minute, int second)
{
    struct tm value = local_value(year, month, day, hour, minute, second);
    return mktime(&value);
}

struct tm normalized_local_value(int year, int month, int day, int hour, int minute, int second)
{
    time_t value = local_epoch(year, month, day, hour, minute, second);
    struct tm local = {};
    localtime_r(&value, &local);
    return local;
}
} // namespace

int main()
{
    assert(ui_visible_weather_sync_active(true, true, false));
    assert(ui_visible_weather_sync_active(true, false, true));
    assert(!ui_visible_weather_sync_active(true, false, false));
    assert(!ui_visible_weather_sync_active(false, true, false));
    assert(!ui_visible_weather_sync_active(false, false, true));

    setenv("TZ", "Asia/Shanghai", 1);
    tzset();

    time_t weather_sync = local_epoch(2026, 7, 13, 10, 15, 0);
    assert(ui_weather_cache_stale(weather_sync, 0));
    assert(!ui_weather_cache_stale(local_epoch(2026, 7, 13, 10, 59, 59), weather_sync));
    assert(ui_weather_cache_stale(local_epoch(2026, 7, 13, 11, 0, 0), weather_sync));
    assert(ui_weather_cache_stale(local_epoch(2026, 7, 14, 10, 15, 0), weather_sync));
    assert(ui_weather_cache_stale(local_epoch(2027, 1, 1, 0, 0, 0),
                                  local_epoch(2026, 12, 31, 23, 59, 59)));

    assert(!ui_weather_cache_stale(3600, 1));
    assert(ui_weather_cache_stale(3601, 1));

    time_t saying_sync = local_epoch(2026, 7, 13, 0, 1, 0);
    struct tm same_day = normalized_local_value(2026, 7, 13, 23, 59, 59);
    struct tm next_day = normalized_local_value(2026, 7, 14, 0, 0, 0);
    assert(ui_daily_saying_cache_stale(same_day, saying_sync, false, saying_sync));
    assert(ui_daily_saying_cache_stale(same_day, saying_sync, true, 0));
    assert(!ui_daily_saying_cache_stale(same_day,
                                        local_epoch(2026, 7, 13, 23, 59, 59),
                                        true,
                                        saying_sync));
    assert(ui_daily_saying_cache_stale(next_day,
                                       local_epoch(2026, 7, 14, 0, 0, 0),
                                       true,
                                       saying_sync));
    assert(ui_daily_saying_cache_stale(normalized_local_value(2027, 1, 1, 0, 0, 0),
                                       local_epoch(2027, 1, 1, 0, 0, 0),
                                       true,
                                       local_epoch(2026, 12, 31, 23, 59, 59)));

    struct tm invalid_local = normalized_local_value(1970, 1, 2, 0, 0, 0);
    assert(!ui_daily_saying_cache_stale(invalid_local, 86400, true, 1));
    assert(ui_daily_saying_cache_stale(invalid_local, 86401, true, 1));
    return 0;
}
