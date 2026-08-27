// 统一网络配置与配网页提交使用的 EventGroup 安全读写和缺失诊断。
#include "network_config_internal.h"

#include "app_constexpr.h"
#include "app_state.h"

namespace {
constexpr const char *kConfigEventReasonFallback = "config";
constexpr const char *kConfigEventActionFallback = "action";
constexpr const char *kConfigEventActionClear = "clear";
constexpr const char *kConfigEventActionSet = "set";
#define CONFIG_EVENT_GROUP_UNAVAILABLE_FORMAT "skip %s event bits for %s: event group unavailable"
const char *config_event_reason_text(const char *reason)
{
    return cstr_nonempty(reason) ? reason : kConfigEventReasonFallback;
}

const char *config_event_action_text(const char *action)
{
    return cstr_nonempty(action) ? action : kConfigEventActionFallback;
}

void log_config_event_group_unavailable(const char *action, const char *reason)
{
    ESP_LOGW(TAG, CONFIG_EVENT_GROUP_UNAVAILABLE_FORMAT,
             config_event_action_text(action),
             config_event_reason_text(reason));
}
} // namespace

void clear_config_event_bits(EventBits_t bits, const char *reason)
{
    if (!g_app_events) {
        log_config_event_group_unavailable(kConfigEventActionClear, reason);
        return;
    }
    xEventGroupClearBits(g_app_events, bits);
}

void set_config_event_bits(EventBits_t bits, const char *reason)
{
    if (!g_app_events) {
        log_config_event_group_unavailable(kConfigEventActionSet, reason);
        return;
    }
    xEventGroupSetBits(g_app_events, bits);
}
