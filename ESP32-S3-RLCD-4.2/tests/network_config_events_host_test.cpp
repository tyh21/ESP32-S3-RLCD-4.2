// 验证网络配置 EventGroup 适配层的空句柄保护、日志回退和位操作转发。
#include "network_config_internal.h"

#include "app_state.h"

#include <assert.h>
#include <string.h>

EventGroupHandle_t g_app_events = nullptr;

namespace {
int g_clear_calls = 0;
int g_set_calls = 0;
EventGroupHandle_t g_last_handle = nullptr;
EventBits_t g_last_bits = 0;
const char *g_last_action = nullptr;
const char *g_last_reason = nullptr;
} // namespace

void record_config_event_log(const char *action, const char *reason)
{
    g_last_action = action;
    g_last_reason = reason;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t event_group, EventBits_t bits)
{
    ++g_clear_calls;
    g_last_handle = event_group;
    g_last_bits = bits;
    return bits;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t event_group, EventBits_t bits)
{
    ++g_set_calls;
    g_last_handle = event_group;
    g_last_bits = bits;
    return bits;
}

int main()
{
    clear_config_event_bits(0x12, nullptr);
    assert(g_clear_calls == 0);
    assert(strcmp(g_last_action, "clear") == 0);
    assert(strcmp(g_last_reason, "config") == 0);

    set_config_event_bits(0x34, "provisioning save");
    assert(g_set_calls == 0);
    assert(strcmp(g_last_action, "set") == 0);
    assert(strcmp(g_last_reason, "provisioning save") == 0);

    g_app_events = reinterpret_cast<EventGroupHandle_t>(0x1);
    clear_config_event_bits(0x56, "reset");
    assert(g_clear_calls == 1);
    assert(g_last_handle == g_app_events);
    assert(g_last_bits == 0x56);

    set_config_event_bits(0x78, "sync");
    assert(g_set_calls == 1);
    assert(g_last_handle == g_app_events);
    assert(g_last_bits == 0x78);
    return 0;
}
