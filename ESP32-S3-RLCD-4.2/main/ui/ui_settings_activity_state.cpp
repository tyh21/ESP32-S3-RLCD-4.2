// 集中管理设置页跨任务活动状态，避免输入、UI 和 OTA 间的数据竞争。
#include "ui_settings_activity_state.h"

#include <atomic>

namespace {
std::atomic<bool> s_page_requested{false};
std::atomic<uint32_t> s_action_sequence{0};
std::atomic<uint32_t> s_last_activity_tick{0};
}

bool settings_page_requested()
{
    return s_page_requested.load(std::memory_order_acquire);
}

void settings_page_request()
{
    s_page_requested.store(true, std::memory_order_release);
}

void settings_page_clear()
{
    s_page_requested.store(false, std::memory_order_release);
}

uint32_t settings_activity_action_sequence()
{
    return s_action_sequence.load(std::memory_order_acquire);
}

uint32_t settings_activity_last_tick()
{
    return s_last_activity_tick.load(std::memory_order_acquire);
}

void settings_activity_record(uint32_t tick)
{
    s_last_activity_tick.store(tick, std::memory_order_release);
}

void settings_activity_record_action(uint32_t tick)
{
    // 先发布活动时间，再递增动作序号；UI 观察到新动作时一定能看到对应时间。
    s_last_activity_tick.store(tick, std::memory_order_relaxed);
    s_action_sequence.fetch_add(1, std::memory_order_release);
}
