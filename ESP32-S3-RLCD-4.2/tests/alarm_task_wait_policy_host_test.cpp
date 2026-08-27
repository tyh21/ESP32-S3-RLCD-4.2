// 验证闹钟任务按分钟边界等待并保留无效时间兜底。
#include "alarm_task_wait_policy.h"

#include <cassert>

int main()
{
    static_assert(kAlarmMinuteMs == 60000U,
                  "alarm minute boundary must remain one minute");
    static_assert(kAlarmInvalidTimePollMs == 1000U,
                  "invalid time must retain one-second recovery checks");
    static_assert(alarm_task_wait_ms(false, true, 12345) == 0,
                  "disabled alarm must sleep until notification");
    static_assert(alarm_task_wait_ms(true, false, 12345) == 1000,
                  "invalid time must use fallback polling");
    static_assert(alarm_task_wait_ms(true, true, -1) == 1000,
                  "negative wall time must use fallback polling");
    static_assert(alarm_task_wait_ms(true, true, 0) == 60000,
                  "exact boundary must wait for the next minute");
    static_assert(alarm_task_wait_ms(true, true, 1) == 59999,
                  "first millisecond must wait to the next boundary");
    static_assert(alarm_task_wait_ms(true, true, 59000) == 1000,
                  "last second must wait one second");
    static_assert(alarm_task_wait_ms(true, true, 59999) == 1,
                  "last millisecond must wait one millisecond");

    assert(alarm_task_wait_ms(true, true, 123456) == 56544);
    assert(alarm_task_wait_ms(false, false, -1) == 0);
    return 0;
}
