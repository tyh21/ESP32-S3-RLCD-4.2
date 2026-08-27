// 调度本地温湿度、电池采样和传感器相关低频后台任务。
#include "sensor_services.h"

#include "app_tick_time.h"
#include "ota_services.h"
#include "ui_views.h"

namespace {
#define SENSOR_INTERVAL_INVALID_LOG_FORMAT "sensor interval invalid: %d"
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kSecondsPerMinute = 60;
constexpr int kUnknownTimeSensorSampleMs = kSecondsPerMinute * kMillisecondsPerSecond;
constexpr uint32_t kHousekeepingOtaPauseDelayMs = 5000;
constexpr uint32_t kHousekeepingFallbackDelayMs = 1000;
constexpr TickType_t kHousekeepingOtaPauseDelay = pdMS_TO_TICKS(kHousekeepingOtaPauseDelayMs);
constexpr TickType_t kHousekeepingFallbackDelay = pdMS_TO_TICKS(kHousekeepingFallbackDelayMs);
constexpr TickType_t kBatteryChargingSampleDelay = pdMS_TO_TICKS(kBatteryChargingSampleMs);
static_assert(kUnknownTimeSensorSampleMs > 0, "unknown-time sensor sample interval must be positive");
static_assert(kSensorSampleNightMinutes >= kSensorSampleDayMinutes,
              "night sensor sample interval must not be faster than day interval");
static_assert(kBatteryChargingSampleMs <
                  kSensorSampleDayMinutes * kSecondsPerMinute * kMillisecondsPerSecond,
              "confirmed charging samples should be faster than idle samples");
static_assert(kHousekeepingOtaPauseDelayMs >= kHousekeepingFallbackDelayMs,
              "housekeeping OTA pause delay should not be shorter than fallback delay");
static_assert(kHousekeepingOtaPauseDelay > 0, "housekeeping OTA pause tick delay must be positive");
static_assert(kHousekeepingFallbackDelay > 0, "housekeeping fallback tick delay must be positive");
static_assert(kBatteryChargingSampleDelay > 0, "battery charging sample tick delay must be positive");

TickType_t next_periodic_sample_tick(TickType_t now,
                                     int day_minutes,
                                     int night_minutes,
                                     int unknown_time_ms)
{
    struct tm local = {};
    if (!is_system_time_plausible(&local)) {
        return now + pdMS_TO_TICKS(unknown_time_ms);
    }
    int interval_seconds = periodic_sample_minutes(local, day_minutes, night_minutes) *
                           kSecondsPerMinute;
    if (interval_seconds <= 0) {
        ESP_LOGW(TAG, SENSOR_INTERVAL_INVALID_LOG_FORMAT, interval_seconds);
    }
    int seconds_to_next = seconds_until_next_periodic_sample(local, interval_seconds);
    return now + pdMS_TO_TICKS(seconds_to_next * kMillisecondsPerSecond);
}

TickType_t next_housekeeping_wake_tick(bool low_battery,
                                       TickType_t now,
                                       TickType_t next_sensor,
                                       TickType_t next_battery)
{
    if (low_battery) {
        return next_battery;
    }
    return app_tick_earlier_deadline(now, next_sensor, next_battery);
}

TickType_t next_battery_wake_after_sample(TickType_t sampled_tick)
{
    BatteryRuntimeSnapshot battery;
    battery_runtime_snapshot_load(&battery);
    if (battery.charging && !battery.animation_complete) {
        return sampled_tick + kBatteryChargingSampleDelay;
    }
    return next_battery_sample_tick(sampled_tick);
}

TickType_t delay_until_housekeeping_wake(TickType_t next_wake)
{
    TickType_t now = xTaskGetTickCount();
    TickType_t remaining = app_tick_deadline_remaining(now, next_wake);
    return remaining > 0 ? remaining : kHousekeepingFallbackDelay;
}

bool system_time_became_valid(bool current_valid, bool previous_valid)
{
    return current_valid && !previous_valid;
}

bool local_sensor_sample_available()
{
    return get_local_sensor_snapshot(nullptr, nullptr, nullptr, nullptr);
}
} // namespace

TickType_t next_sensor_sample_tick(TickType_t now)
{
    return next_periodic_sample_tick(now,
                                     kSensorSampleDayMinutes,
                                     kSensorSampleNightMinutes,
                                     kUnknownTimeSensorSampleMs);
}

TickType_t next_battery_sample_tick(TickType_t now)
{
    return next_periodic_sample_tick(
        now,
        kSensorSampleDayMinutes,
        kSensorSampleNightMinutes,
        kUnknownTimeSensorSampleMs);
}

void housekeeping_task(void *)
{
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t next_sensor = next_sensor_sample_tick(start_tick);
    TickType_t next_battery = next_battery_sample_tick(start_tick);
    bool last_time_valid = is_system_time_plausible();
    if (!battery_low_mode_load() && !local_sensor_sample_available()) {
        sample_sensor();
    }
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if (ota_flow_active()) {
            vTaskDelay(kHousekeepingOtaPauseDelay);
            next_sensor = next_sensor_sample_tick(xTaskGetTickCount());
            next_battery = next_battery_sample_tick(xTaskGetTickCount());
            continue;
        }
        bool time_valid = is_system_time_plausible();
        if (system_time_became_valid(time_valid, last_time_valid)) {
            next_sensor = next_sensor_sample_tick(now);
            next_battery = next_battery_sample_tick(now);
        }
        last_time_valid = time_valid;
        if (app_tick_deadline_reached(now, next_sensor)) {
            if (!battery_low_mode_load()) {
                sample_sensor();
            }
            next_sensor = next_sensor_sample_tick(xTaskGetTickCount());
        }
        if (app_tick_deadline_reached(now, next_battery)) {
            bool was_low_battery = battery_low_mode_load();
            sample_battery();
            TickType_t after_battery = xTaskGetTickCount();
            if (was_low_battery && !battery_low_mode_load()) {
                next_sensor = next_sensor_sample_tick(after_battery);
            }
            next_battery = next_battery_wake_after_sample(after_battery);
        }
        TickType_t next_wake = next_housekeeping_wake_tick(battery_low_mode_load(),
                                                           xTaskGetTickCount(),
                                                           next_sensor,
                                                           next_battery);
        vTaskDelay(delay_until_housekeeping_wake(next_wake));
    }
}
