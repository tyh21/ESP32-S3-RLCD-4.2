// 声明可见页面天气与每日文字缓存的新鲜度纯判断。
#pragma once

#include <time.h>

constexpr bool ui_visible_weather_sync_active(bool normal_work_mode,
                                              bool weather_clock_visible,
                                              bool weather_board_visible)
{
    return normal_work_mode &&
           (weather_clock_visible || weather_board_visible);
}

bool ui_weather_cache_stale(time_t now_value, time_t last_sync_time);
bool ui_daily_saying_cache_stale(const struct tm &local_value,
                                 time_t now_value,
                                 bool snapshot_ready,
                                 time_t last_sync_time);
