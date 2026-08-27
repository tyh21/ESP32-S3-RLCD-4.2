// 声明不依赖 ADC、UI 和全局状态的电池充电趋势状态机。
#pragma once

#include <stdint.h>

struct BatteryChargingTracker {
    int rise_samples = 0;
    int stop_samples = 0;
    float peak_voltage = 0.0f;
    bool peak_tick_set = false;
    uint32_t last_peak_tick = 0;
};

struct BatteryChargingState {
    bool charging = false;
    bool animation_complete = false;
};

struct BatteryChargingPolicy {
    float valid_previous_voltage_min = 0.0f;
    float rise_voltage = 0.0f;
    float stop_voltage = 0.0f;
    int rise_samples_required = 0;
    int stop_samples_required = 0;
    int animation_stop_percent = 0;
    uint32_t animation_idle_ticks = 0;
};

struct BatteryChargingInput {
    float previous_voltage = 0.0f;
    float current_voltage = 0.0f;
    int percent = 0;
    uint32_t now_tick = 0;
};

void reset_battery_charging_tracker(BatteryChargingTracker *tracker);
bool update_battery_charging_state(const BatteryChargingInput &input,
                                   const BatteryChargingPolicy &policy,
                                   BatteryChargingTracker *tracker,
                                   BatteryChargingState *state);
