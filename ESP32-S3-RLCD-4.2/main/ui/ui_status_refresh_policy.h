// 声明工作页状态栏事件刷新与低频兜底判断的纯逻辑接口。
#pragma once

#include <stdint.h>

inline constexpr uint32_t kUiStatusFallbackRefreshMs = 60U * 1000U;

struct UiStatusRefreshSnapshot {
    uint32_t sensor_version = 0;
    uint32_t alarm_version = 0;
    bool chime_enabled = false;
    bool wifi_radio_on = false;
};

bool ui_status_refresh_inputs_changed(const UiStatusRefreshSnapshot &current,
                                      const UiStatusRefreshSnapshot &previous);
bool ui_status_refresh_due(const UiStatusRefreshSnapshot &current,
                           const UiStatusRefreshSnapshot &previous,
                           bool previous_valid,
                           bool fallback_elapsed);
