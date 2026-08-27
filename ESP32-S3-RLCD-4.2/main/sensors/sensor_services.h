// 声明传感器、电池、历史数据和电源服务的公共接口。
#pragma once
#include "app_state.h"
#include "sensor_time.h"

inline constexpr int kSensorSampleDayMinutes = 1;
inline constexpr int kSensorSampleNightMinutes = 2;

struct PowerLockDepthSnapshot {
    int network = 0;
    int audio = 0;
    int audio_wake = 0;
    int audio_cpu = 0;
};

void init_power_management();
[[nodiscard]] bool acquire_network_awake_lock();
void release_network_awake_lock();
bool network_awake_lock_active();
bool get_power_lock_depth_snapshot(PowerLockDepthSnapshot *out);
[[nodiscard]] bool acquire_audio_awake_lock();
void release_audio_awake_lock();
void set_audio_performance_mode(bool enabled);
void restore_system_time_from_rtc();
void sync_rtc_from_system_time();
void release_battery_gauge();
bool init_battery_gauge();
int battery_percent_from_voltage(float voltage);
bool read_battery_percent(int *percent);
void sample_battery();
void reset_hourly_sensor_history();
void load_hourly_sensor_history();
void record_hourly_sensor_sample(float temp, float humi);
bool get_hourly_sensor_history_snapshot(HourlySensorHistoryBlob *history, uint32_t *version);
void update_sensor_history(float temp, float humi);
void sample_sensor();
bool get_local_sensor_snapshot(float *temperature,
                               float *humidity,
                               int *temperature_trend,
                               int *humidity_trend);
uint32_t local_sensor_state_version();
TickType_t next_sensor_sample_tick(TickType_t now);
TickType_t next_battery_sample_tick(TickType_t now);
void housekeeping_task(void *);
