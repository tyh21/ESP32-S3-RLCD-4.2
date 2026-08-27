// 集中发布设置页手动同步状态，避免网络与 UI 读取到混合字段。
#include "ui_settings_sync_state.h"

#include "freertos/FreeRTOS.h"

namespace {
portMUX_TYPE s_settings_sync_mux = portMUX_INITIALIZER_UNLOCKED;
SettingsSyncStateSnapshot s_settings_sync_state;
}

void settings_sync_state_load(SettingsSyncStateSnapshot *out)
{
    if (!out) {
        return;
    }
    portENTER_CRITICAL(&s_settings_sync_mux);
    *out = s_settings_sync_state;
    portEXIT_CRITICAL(&s_settings_sync_mux);
}

void settings_sync_state_begin(int operation, uint32_t deadline_tick)
{
    portENTER_CRITICAL(&s_settings_sync_mux);
    s_settings_sync_state.operation = operation;
    s_settings_sync_state.deadline_tick = deadline_tick;
    portEXIT_CRITICAL(&s_settings_sync_mux);
}

bool settings_sync_state_clear_if(int operation)
{
    portENTER_CRITICAL(&s_settings_sync_mux);
    bool matched = s_settings_sync_state.operation == operation;
    if (matched) {
        s_settings_sync_state = {};
    }
    portEXIT_CRITICAL(&s_settings_sync_mux);
    return matched;
}
