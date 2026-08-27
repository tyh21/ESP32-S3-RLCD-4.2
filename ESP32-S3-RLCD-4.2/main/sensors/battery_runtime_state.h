// 声明跨任务一致读取的电池运行态快照。
#pragma once

#include <stdint.h>
#include <time.h>

struct BatteryRuntimeSnapshot {
    int percent = -1;
    float voltage = -1.0f;
    bool charging = false;
    bool animation_complete = false;
    time_t last_charge_time = 0;
    uint32_t version = 0;
    bool low_battery_mode = false;
};

void battery_runtime_snapshot_load(BatteryRuntimeSnapshot *out);
void battery_runtime_snapshot_store(const BatteryRuntimeSnapshot &snapshot);
void battery_runtime_voltage_store(float voltage);
int battery_percent_load();
bool battery_charging_load();
bool battery_low_mode_load();
bool battery_low_mode_for_percent(bool current_mode,
                                  int percent,
                                  int enter_percent,
                                  int exit_percent);
bool battery_runtime_low_mode_update(int enter_percent, int exit_percent);
