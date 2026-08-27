// 定义低刷新工作页按键任务进入 GPIO 事件等待的纯判断策略。
#pragma once

constexpr bool button_task_can_wait_for_edge(bool edge_wakeup_ready,
                                             bool low_refresh_idle,
                                             bool boot_pressed,
                                             bool key_pressed,
                                             bool press_tracking_active)
{
    return edge_wakeup_ready &&
           low_refresh_idle &&
           !boot_pressed &&
           !key_pressed &&
           !press_tracking_active;
}
