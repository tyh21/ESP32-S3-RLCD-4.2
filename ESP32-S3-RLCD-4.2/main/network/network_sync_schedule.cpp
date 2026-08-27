// 计算联网同步任务本轮应执行的项目，不访问 Wi-Fi、事件组或全局状态。
#include "network_sync_schedule.h"

#include <limits.h>

namespace {
constexpr int64_t kMicrosecondsPerMillisecond = 1000;
constexpr uint32_t kMillisecondsPerSecond = 1000;
constexpr time_t kSecondsPerMinute = 60;
constexpr uint32_t kIdleFallbackWaitMs = 60 * kSecondsPerMinute * kMillisecondsPerSecond;
constexpr uint32_t kIdleMaximumWaitMs = 24 * 60 * kSecondsPerMinute * kMillisecondsPerSecond;
constexpr uint32_t kIdleMinimumWaitMs = 1000;
constexpr size_t kBootHttpsMinimumInternalFree = 48 * 1024;
constexpr size_t kBootHttpsMinimumInternalLargest = 24 * 1024;
constexpr size_t kBootHttpsMinimumDmaLargest = 16 * 1024;
constexpr int64_t kStartupPressureWindowUs = 60LL * 1000 * 1000;
constexpr int64_t kStartupVisibleAutoSyncDelayUs = 30LL * 1000 * 1000;
constexpr uint32_t kWeatherRequestSettleDelayMs = 120;
constexpr uint32_t kStartupWeatherRequestSettleDelayMs = 300;
constexpr uint32_t kNetworkOperationSettleDelayMs = 250;
constexpr uint32_t kStartupNetworkOperationSettleDelayMs = 1000;
static_assert(kIdleMinimumWaitMs > 0, "network idle minimum wait must be positive");
static_assert(kIdleFallbackWaitMs >= kIdleMinimumWaitMs,
              "network idle fallback wait must cover the minimum wait");
static_assert(kIdleMaximumWaitMs >= kIdleFallbackWaitMs,
              "network idle maximum wait must cover the fallback wait");
static_assert(kBootHttpsMinimumInternalFree >= kBootHttpsMinimumInternalLargest,
              "boot HTTPS free-memory threshold must cover the largest-block threshold");
static_assert(kBootHttpsMinimumInternalLargest >= kBootHttpsMinimumDmaLargest,
              "boot HTTPS internal block threshold must cover the DMA block threshold");
static_assert(kStartupPressureWindowUs > 0,
              "startup pressure window must be positive");
static_assert(kStartupVisibleAutoSyncDelayUs > 0 &&
                  kStartupVisibleAutoSyncDelayUs < kStartupPressureWindowUs,
              "visible auto sync delay must fit the startup pressure window");
static_assert(kStartupWeatherRequestSettleDelayMs > kWeatherRequestSettleDelayMs,
              "startup HTTPS requests must use the longer settle delay");
static_assert(kStartupNetworkOperationSettleDelayMs > kNetworkOperationSettleDelayMs,
              "startup network operations must use the longer settle delay");

time_t earliest_pending_boot_sync(const NetworkSyncScheduleInput &input)
{
    time_t next = 0;
    if (input.boot_weather_due && input.boot_weather_due_at > input.now) {
        next = input.boot_weather_due_at;
    }
    if (input.boot_saying_due &&
        input.boot_saying_due_at > input.now &&
        (next == 0 || input.boot_saying_due_at < next)) {
        next = input.boot_saying_due_at;
    }
    return next;
}

uint32_t future_deadline_wait_ms(time_t now,
                                 time_t deadline,
                                 uint32_t fallback_ms)
{
    if (deadline <= now) {
        return fallback_ms;
    }
    int64_t seconds = static_cast<int64_t>(deadline) - static_cast<int64_t>(now);
    if (seconds >= static_cast<int64_t>(kIdleMaximumWaitMs) /
                       kMillisecondsPerSecond) {
        return kIdleMaximumWaitMs;
    }
    return static_cast<uint32_t>(seconds * kMillisecondsPerSecond);
}
} // namespace

NetworkSyncSchedule calculate_network_sync_schedule(const NetworkSyncScheduleInput &input)
{
    NetworkSyncSchedule schedule = {};
    schedule.boot_weather_ready = input.boot_weather_due && input.now >= input.boot_weather_due_at;
    bool boot_saying_time_ready = input.boot_saying_due && input.now >= input.boot_saying_due_at;
    schedule.stagger_boot_saying_after_weather =
        schedule.boot_weather_ready && input.boot_saying_due &&
        !input.provisioning_sync_due && !input.manual_saying_due;
    schedule.boot_saying_ready = boot_saying_time_ready &&
                                 !schedule.stagger_boot_saying_after_weather;
    schedule.weather_due = input.have_weather_key &&
                           !input.low_battery_mode &&
                           (input.manual_weather_due ||
                            input.provisioning_sync_due ||
                            schedule.boot_weather_ready);
    schedule.ntp_due = (input.manual_ntp_due ||
                        input.provisioning_sync_due ||
                        input.boot_ntp_due ||
                        input.daily_ntp_due) &&
                       input.now >= input.next_ntp_retry_at;
    schedule.ntp_retry_required = input.boot_ntp_due || input.daily_ntp_due;
    schedule.saying_due = !input.low_battery_mode &&
                          (input.manual_saying_due ||
                           input.provisioning_sync_due ||
                           schedule.boot_saying_ready);
    schedule.next_boot_due_at = earliest_pending_boot_sync(input);
    return schedule;
}

int network_boot_budget_remaining_ms(int64_t deadline_us, int64_t now_us)
{
    if (deadline_us <= 0) {
        return INT32_MAX;
    }
    int64_t remaining_us = deadline_us - now_us;
    if (remaining_us <= 0) {
        return 0;
    }
    int64_t remaining_ms = remaining_us / kMicrosecondsPerMillisecond;
    return remaining_ms > INT32_MAX ? INT32_MAX : static_cast<int>(remaining_ms);
}

uint32_t network_idle_wait_ms(time_t now,
                              time_t next_boot_due_at,
                              time_t next_ntp_retry_at,
                              time_t next_daily_ntp_at)
{
    uint32_t wait_ms = next_daily_ntp_at > now
                           ? future_deadline_wait_ms(now,
                                                     next_daily_ntp_at,
                                                     kIdleFallbackWaitMs)
                           : kIdleFallbackWaitMs;
    if (next_boot_due_at > now) {
        uint32_t boot_wait = future_deadline_wait_ms(now,
                                                     next_boot_due_at,
                                                     kIdleFallbackWaitMs);
        if (boot_wait < wait_ms) {
            wait_ms = boot_wait;
        }
    }
    if (next_ntp_retry_at > now) {
        uint32_t ntp_wait = future_deadline_wait_ms(now,
                                                    next_ntp_retry_at,
                                                    kIdleFallbackWaitMs);
        if (ntp_wait < wait_ms) {
            wait_ms = ntp_wait;
        }
    }
    if (wait_ms < kIdleMinimumWaitMs) {
        wait_ms = kIdleMinimumWaitMs;
    } else if (wait_ms > kIdleMaximumWaitMs) {
        wait_ms = kIdleMaximumWaitMs;
    }
    return wait_ms;
}

bool network_cache_age_is_fresh(time_t now, time_t cached_at, time_t max_age)
{
    return cached_at > 0 && max_age > 0 && now >= cached_at &&
           now - cached_at < max_age;
}

bool network_cache_local_day_matches(const struct tm &now_local,
                                     const struct tm &cached_local)
{
    return now_local.tm_year == cached_local.tm_year &&
           now_local.tm_yday == cached_local.tm_yday;
}

bool network_cache_local_hour_matches(const struct tm &now_local,
                                      const struct tm &cached_local)
{
    return network_cache_local_day_matches(now_local, cached_local) &&
           now_local.tm_hour == cached_local.tm_hour;
}

bool network_boot_https_memory_sufficient(size_t internal_free,
                                          size_t internal_largest,
                                          size_t dma_largest)
{
    return internal_free >= kBootHttpsMinimumInternalFree &&
           internal_largest >= kBootHttpsMinimumInternalLargest &&
           dma_largest >= kBootHttpsMinimumDmaLargest;
}

bool network_startup_pressure_window_active(bool startup_screen_active,
                                            int64_t uptime_us)
{
    return startup_screen_active ||
           (uptime_us >= 0 && uptime_us < kStartupPressureWindowUs);
}

uint32_t network_weather_request_settle_delay_ms(bool startup_pressure_active)
{
    return startup_pressure_active
               ? kStartupWeatherRequestSettleDelayMs
               : kWeatherRequestSettleDelayMs;
}

uint32_t network_inter_operation_settle_delay_ms(bool startup_pressure_active)
{
    return startup_pressure_active
               ? kStartupNetworkOperationSettleDelayMs
               : kNetworkOperationSettleDelayMs;
}

bool network_visible_auto_sync_allowed(int64_t uptime_us)
{
    return uptime_us < 0 || uptime_us >= kStartupVisibleAutoSyncDelayUs;
}

bool network_startup_followup_https_allowed(bool startup_pressure_active,
                                            size_t internal_free,
                                            size_t internal_largest,
                                            size_t dma_largest)
{
    return !startup_pressure_active ||
           network_boot_https_memory_sufficient(internal_free,
                                                internal_largest,
                                                dma_largest);
}

bool network_automatic_boot_https_allowed(bool startup_screen_active,
                                          int64_t uptime_us,
                                          size_t internal_free,
                                          size_t internal_largest,
                                          size_t dma_largest)
{
    return network_startup_followup_https_allowed(
        network_startup_pressure_window_active(startup_screen_active, uptime_us),
        internal_free,
        internal_largest,
        dma_largest);
}

bool network_boot_weather_due_after_update(bool boot_weather_due,
                                           bool boot_weather_ready,
                                           bool resource_deferred)
{
    return boot_weather_ready ? resource_deferred : boot_weather_due;
}
