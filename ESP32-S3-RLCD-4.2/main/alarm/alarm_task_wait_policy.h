// 计算闹钟后台任务到下一分钟边界的低功耗等待时间。
#pragma once

#include <stdint.h>

inline constexpr uint32_t kAlarmMinuteMs = 60U * 1000U;
inline constexpr uint32_t kAlarmInvalidTimePollMs = 1000U;

constexpr uint32_t alarm_task_wait_ms(bool alarm_enabled,
                                      bool time_valid,
                                      int64_t wall_clock_ms)
{
    if (!alarm_enabled) {
        return 0;
    }
    if (!time_valid || wall_clock_ms < 0) {
        return kAlarmInvalidTimePollMs;
    }
    const uint32_t elapsed_in_minute =
        static_cast<uint32_t>(wall_clock_ms % kAlarmMinuteMs);
    return elapsed_in_minute == 0
               ? kAlarmMinuteMs
               : kAlarmMinuteMs - elapsed_in_minute;
}

static_assert(kAlarmMinuteMs > kAlarmInvalidTimePollMs,
              "valid alarm minute wait must exceed invalid-time fallback");
