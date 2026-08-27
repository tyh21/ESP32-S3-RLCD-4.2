// 声明联网同步任务每轮到期项目与下一唤醒时间的纯计算接口。
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct NetworkSyncScheduleInput {
    time_t now = 0;
    time_t next_ntp_retry_at = 0;
    time_t boot_weather_due_at = 0;
    time_t boot_saying_due_at = 0;
    bool have_weather_key = false;
    bool low_battery_mode = false;
    bool provisioning_sync_due = false;
    bool manual_ntp_due = false;
    bool manual_weather_due = false;
    bool manual_saying_due = false;
    bool boot_ntp_due = false;
    bool daily_ntp_due = false;
    bool boot_weather_due = false;
    bool boot_saying_due = false;
};

struct NetworkSyncSchedule {
    bool boot_weather_ready = false;
    bool boot_saying_ready = false;
    bool stagger_boot_saying_after_weather = false;
    bool ntp_due = false;
    bool ntp_retry_required = false;
    bool weather_due = false;
    bool saying_due = false;
    time_t next_boot_due_at = 0;
};

NetworkSyncSchedule calculate_network_sync_schedule(const NetworkSyncScheduleInput &input);
int network_boot_budget_remaining_ms(int64_t deadline_us, int64_t now_us);
uint32_t network_idle_wait_ms(time_t now,
                              time_t next_boot_due_at,
                              time_t next_ntp_retry_at,
                              time_t next_daily_ntp_at);
bool network_cache_age_is_fresh(time_t now, time_t cached_at, time_t max_age);
bool network_cache_local_hour_matches(const struct tm &now_local,
                                      const struct tm &cached_local);
bool network_cache_local_day_matches(const struct tm &now_local,
                                     const struct tm &cached_local);
bool network_boot_https_memory_sufficient(size_t internal_free,
                                          size_t internal_largest,
                                          size_t dma_largest);
bool network_startup_pressure_window_active(bool startup_screen_active,
                                            int64_t uptime_us);
uint32_t network_weather_request_settle_delay_ms(bool startup_pressure_active);
uint32_t network_inter_operation_settle_delay_ms(bool startup_pressure_active);
bool network_visible_auto_sync_allowed(int64_t uptime_us);
bool network_startup_followup_https_allowed(bool startup_pressure_active,
                                            size_t internal_free,
                                            size_t internal_largest,
                                            size_t dma_largest);
bool network_automatic_boot_https_allowed(bool startup_screen_active,
                                          int64_t uptime_us,
                                          size_t internal_free,
                                          size_t internal_largest,
                                          size_t dma_largest);
bool network_boot_weather_due_after_update(bool boot_weather_due,
                                           bool boot_weather_ready,
                                           bool resource_deferred);
