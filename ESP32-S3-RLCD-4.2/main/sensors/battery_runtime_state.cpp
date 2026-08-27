// 集中发布电池相关字段，避免采样任务与消费者读取到混合状态。
#include "battery_runtime_state.h"

#include "freertos/FreeRTOS.h"

namespace {
portMUX_TYPE s_battery_runtime_mux = portMUX_INITIALIZER_UNLOCKED;
BatteryRuntimeSnapshot s_battery_runtime;
}

void battery_runtime_snapshot_load(BatteryRuntimeSnapshot *out)
{
    if (!out) {
        return;
    }
    portENTER_CRITICAL(&s_battery_runtime_mux);
    *out = s_battery_runtime;
    portEXIT_CRITICAL(&s_battery_runtime_mux);
}

void battery_runtime_snapshot_store(const BatteryRuntimeSnapshot &snapshot)
{
    portENTER_CRITICAL(&s_battery_runtime_mux);
    s_battery_runtime = snapshot;
    portEXIT_CRITICAL(&s_battery_runtime_mux);
}

void battery_runtime_voltage_store(float voltage)
{
    portENTER_CRITICAL(&s_battery_runtime_mux);
    s_battery_runtime.voltage = voltage;
    portEXIT_CRITICAL(&s_battery_runtime_mux);
}

int battery_percent_load()
{
    BatteryRuntimeSnapshot snapshot;
    battery_runtime_snapshot_load(&snapshot);
    return snapshot.percent;
}

bool battery_charging_load()
{
    BatteryRuntimeSnapshot snapshot;
    battery_runtime_snapshot_load(&snapshot);
    return snapshot.charging;
}

bool battery_low_mode_load()
{
    BatteryRuntimeSnapshot snapshot;
    battery_runtime_snapshot_load(&snapshot);
    return snapshot.low_battery_mode;
}

bool battery_low_mode_for_percent(bool current_mode,
                                  int percent,
                                  int enter_percent,
                                  int exit_percent)
{
    if (percent < 0) {
        return current_mode;
    }
    if (!current_mode && percent < enter_percent) {
        return true;
    }
    if (current_mode && percent >= exit_percent) {
        return false;
    }
    return current_mode;
}

bool battery_runtime_low_mode_update(int enter_percent, int exit_percent)
{
    portENTER_CRITICAL(&s_battery_runtime_mux);
    bool next_mode = battery_low_mode_for_percent(s_battery_runtime.low_battery_mode,
                                                  s_battery_runtime.percent,
                                                  enter_percent,
                                                  exit_percent);
    bool changed = next_mode != s_battery_runtime.low_battery_mode;
    s_battery_runtime.low_battery_mode = next_mode;
    portEXIT_CRITICAL(&s_battery_runtime_mux);
    return changed;
}
