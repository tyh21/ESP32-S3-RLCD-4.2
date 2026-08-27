// 实现工作页状态栏按变化刷新策略，不访问 LVGL、任务或硬件状态。
#include "ui_status_refresh_policy.h"

static_assert(kUiStatusFallbackRefreshMs > 0,
              "UI status fallback refresh interval must be positive");

bool ui_status_refresh_inputs_changed(const UiStatusRefreshSnapshot &current,
                                      const UiStatusRefreshSnapshot &previous)
{
    return current.sensor_version != previous.sensor_version ||
           current.alarm_version != previous.alarm_version ||
           current.chime_enabled != previous.chime_enabled ||
           current.wifi_radio_on != previous.wifi_radio_on;
}

bool ui_status_refresh_due(const UiStatusRefreshSnapshot &current,
                           const UiStatusRefreshSnapshot &previous,
                           bool previous_valid,
                           bool fallback_elapsed)
{
    return !previous_valid ||
           fallback_elapsed ||
           ui_status_refresh_inputs_changed(current, previous);
}
