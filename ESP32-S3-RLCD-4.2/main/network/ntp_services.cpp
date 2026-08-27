// 执行 NTP 时间同步并维护系统可信时间状态。
#include "network_services.h"

#include "alarm_services.h"
#include "app_constexpr.h"
#include "app_time_constants.h"
#include "sensor_services.h"

#include "sdkconfig.h"

#define NTP_SYNCED_LOG_FORMAT "ntp synced: %04d-%02d-%02d %02d:%02d:%02d"
#define NTP_TIMEOUT_LOG_FORMAT "ntp sync timeout retries=%d poll_ms=%lu"
#define NTP_INVALID_RETRY_COUNT_LOG_FORMAT "ntp sync invalid retry count: %d"

namespace {
bool s_ntp_started = false;
portMUX_TYPE s_ntp_state_mux = portMUX_INITIALIZER_UNLOCKED;
time_t s_last_ntp_sync_time = 0;
constexpr const char *const kNtpServers[] = {
    "pool.ntp.org",
    "ntp.aliyun.com",
    "time.windows.com",
};
constexpr size_t kNtpServerCount = array_count(kNtpServers);
constexpr size_t kDefaultConfiguredNtpServerSlots = 1;
#ifdef CONFIG_LWIP_SNTP_MAX_SERVERS
constexpr size_t kConfiguredNtpServerSlots = CONFIG_LWIP_SNTP_MAX_SERVERS;
#else
constexpr size_t kConfiguredNtpServerSlots = kDefaultConfiguredNtpServerSlots;
#endif
constexpr uint32_t kNtpPollDelayMs = 1000;
static_assert(kNtpServerCount > 0, "at least one NTP server is required");
static_assert(kConfiguredNtpServerSlots > 0, "SNTP must support at least one configured server");

constexpr size_t min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

constexpr size_t kActiveNtpServerCount = min_size(kNtpServerCount, kConfiguredNtpServerSlots);
constexpr TickType_t kNtpPollDelay = pdMS_TO_TICKS(kNtpPollDelayMs);
constexpr const char *kNtpTimeSyncedEventUnavailableLog = "skip time synced event bit: app events unavailable";

static_assert(cstr_array_nonempty(kNtpServers), "NTP server names must be non-empty");
static_assert(kActiveNtpServerCount > 0, "active NTP server count must be positive");
static_assert(kActiveNtpServerCount <= kNtpServerCount, "active NTP server count must fit server list");
static_assert(kActiveNtpServerCount <= kConfiguredNtpServerSlots, "active NTP server count must fit SNTP slots");
static_assert(kNtpPollDelayMs > 0, "NTP poll delay must be positive");
static_assert(kNtpPollDelay > 0, "NTP poll tick delay must be positive");

void set_time_synced_event_bit()
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", kNtpTimeSyncedEventUnavailableLog);
        return;
    }
    xEventGroupSetBits(g_app_events, kTimeSyncedBit);
}

void configure_ntp_servers()
{
    for (size_t i = 0; i < kActiveNtpServerCount; ++i) {
        esp_sntp_setservername(i, kNtpServers[i]);
    }
}

void start_or_restart_ntp()
{
    if (!s_ntp_started) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        configure_ntp_servers();
        esp_sntp_init();
        s_ntp_started = true;
        return;
    }
    esp_sntp_restart();
}

void log_ntp_synced_time(const struct tm &local)
{
    ESP_LOGI(TAG, NTP_SYNCED_LOG_FORMAT,
             local.tm_year + kTmYearOffset, local.tm_mon + kTmMonthOffset, local.tm_mday,
             local.tm_hour, local.tm_min, local.tm_sec);
}

bool ntp_synced_time_available(struct tm *local)
{
    return local &&
           esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED &&
           is_system_time_plausible(local);
}

bool wait_for_ntp_synced_time(int max_retries, struct tm *synced_time)
{
    if (!synced_time) {
        return false;
    }
    for (int retry = 0; retry < max_retries; ++retry) {
        struct tm local = {};
        if (ntp_synced_time_available(&local)) {
            *synced_time = local;
            return true;
        }
        vTaskDelay(kNtpPollDelay);
    }
    return false;
}
} // namespace

time_t get_last_ntp_sync_time()
{
    portENTER_CRITICAL(&s_ntp_state_mux);
    const time_t last_sync_time = s_last_ntp_sync_time;
    portEXIT_CRITICAL(&s_ntp_state_mux);
    return last_sync_time;
}

bool perform_ntp_sync(int max_retries)
{
    if (max_retries <= 0) {
        ESP_LOGW(TAG, NTP_INVALID_RETRY_COUNT_LOG_FORMAT, max_retries);
        return false;
    }
    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    start_or_restart_ntp();

    struct tm local = {};
    if (wait_for_ntp_synced_time(max_retries, &local)) {
        sync_rtc_from_system_time();
        const time_t sync_time = time(nullptr);
        portENTER_CRITICAL(&s_ntp_state_mux);
        s_last_ntp_sync_time = sync_time;
        portEXIT_CRITICAL(&s_ntp_state_mux);
        alarm_notify_time_changed();
        set_time_synced_event_bit();
        log_ntp_synced_time(local);
        return true;
    }
    ESP_LOGW(TAG, NTP_TIMEOUT_LOG_FORMAT,
             max_retries,
             (unsigned long)kNtpPollDelayMs);
    return false;
}
