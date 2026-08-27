// 验证 UI 主循环秒/分钟边界和最短轮询候选选择规则。
#include "ui_loop_schedule.h"

#include <assert.h>
#include <stdint.h>

int main()
{
    assert(ui_next_second_delay_ms(0) == 1005);
    assert(ui_next_second_delay_ms(500000) == 505);
    assert(ui_next_second_delay_ms(999000) == 10);
    assert(ui_next_second_delay_ms(999999) == 10);
    assert(ui_next_second_delay_ms(1000000) == 1005);
    assert(ui_next_second_delay_ms(-1) == 1005);

    assert(ui_next_minute_delay_ms(0) == 60005);
    assert(ui_next_minute_delay_ms(1) == 59005);
    assert(ui_next_minute_delay_ms(59) == 1005);
    assert(ui_next_minute_delay_ms(60) == 60005);
    assert(ui_next_minute_delay_ms(-1) == 60005);

    assert(ui_pomodoro_boundary_delay_ms(0) == 0);
    assert(ui_pomodoro_boundary_delay_ms(995) == 1000);
    assert(ui_nonzero_delay_ticks(0) == 1);
    assert(ui_nonzero_delay_ticks(25) == 25);

    const uint32_t candidates[] = {250, 0, 50, 75, 0};
    assert(ui_shortest_delay_ticks(candidates, 5) == 50);
    const uint32_t leading_zero[] = {0, 100, 25};
    assert(ui_shortest_delay_ticks(leading_zero, 3) == 25);
    const uint32_t all_zero[] = {0, 0, 0};
    assert(ui_shortest_delay_ticks(all_zero, 3) == 0);
    assert(ui_shortest_delay_ticks(nullptr, 3) == 0);
    assert(ui_shortest_delay_ticks(candidates, 0) == 0);
    return 0;
}
