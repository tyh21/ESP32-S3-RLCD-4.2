// 统一网络同步请求快照、用户反馈和事件位清理顺序。
#include "network_sync_requests.h"

#include "network_diagnostics_catalog.h"
#include "network_services.h"
#include "ui_settings_feedback.h"
#include "ui_task_notify.h"
#include "wifi_portal_state.h"
#include "wifi_radio_state.h"

namespace {

constexpr const char *kNetworkStatusOfflineModeEnabled = "离线模式已开启";
constexpr const char *kNetworkStatusWifiNotConfigured = "未配置 WiFi";
constexpr const char *kNetworkDiagLocalIpPlaceholder = "本地IP: --";
constexpr const char *kNetworkDiagPublicIpPlaceholder = "公网IP: --";
constexpr const char *kNetworkDiagIpLocationWifiNotConfigured = "IP定位: WiFi未配置";
constexpr const char *kNetworkDiagDnsUnchecked = "DNS: 未检测";
constexpr const char *kNetworkDiagWeatherUnchecked = "天气: 未检测";
constexpr const char *kNetworkDiagNtpUnchecked = "NTP: 未检测";
constexpr const char *kNetworkDiagSayingUnchecked = "一言: 未检测";
constexpr const char *kNetworkDiagInternetUnchecked = "公网: 未检测";
constexpr const char *kNetworkDiagOtaSourceUnchecked = "OTA源: 未检测";
constexpr const char *kNetworkSyncTimeComplete = "时间同步完成";
constexpr const char *kNetworkSyncWeatherComplete = "天气同步完成";
constexpr const char *kNetworkSyncSayingComplete = "一言更新完成";
constexpr const char *kNetworkSyncNetworkDiagComplete = "网络检测完成";
constexpr const char *kNetworkSyncTimeFailed = "时间同步失败";
constexpr const char *kNetworkSyncWeatherFailed = "天气同步失败";
constexpr const char *kNetworkSyncSayingFailed = "一言更新失败";

void finish_requested_settings_sync(bool requested,
                                    SettingsSyncOp op,
                                    const char *status,
                                    EventBits_t bit,
                                    bool clear_bit)
{
    if (!requested) {
        return;
    }
    finish_settings_sync(op, status);
    if (clear_bit) {
        xEventGroupClearBits(g_app_events, bit);
    }
}

void finish_requested_manual_syncs(const NetworkSyncRequestSnapshot &requests,
                                   const char *status,
                                   bool clear_bits)
{
    finish_requested_settings_sync(requests.manual_ntp,
                                   kSettingsSyncNtp,
                                   status,
                                   kManualNtpSyncBit,
                                   clear_bits);
    finish_requested_settings_sync(requests.manual_weather,
                                   kSettingsSyncWeather,
                                   status,
                                   kManualWeatherSyncBit,
                                   clear_bits);
    finish_requested_settings_sync(requests.manual_saying,
                                   kSettingsSyncSaying,
                                   status,
                                   kManualSayingSyncBit,
                                   clear_bits);
}

} // namespace

NetworkSyncRequestSnapshot snapshot_network_sync_requests()
{
    // A loop must schedule and finish the same event snapshot even if another
    // task raises a new request while HTTPS is in progress.
    EventBits_t bits = xEventGroupGetBits(g_app_events);
    NetworkSyncRequestSnapshot requests;
    requests.provisioning = (bits & kProvisioningSyncBit) != 0;
    requests.manual_ntp = (bits & kManualNtpSyncBit) != 0;
    requests.manual_weather = (bits & kManualWeatherSyncBit) != 0;
    requests.manual_saying = (bits & kManualSayingSyncBit) != 0;
    requests.diagnostics = (bits & kNetworkDiagBit) != 0;
    return requests;
}

void finish_settings_sync_and_clear_bit(SettingsSyncOp op,
                                        const char *status,
                                        EventBits_t bit)
{
    finish_requested_settings_sync(true, op, status, bit, true);
}

void set_network_diag_unavailable(const char *ip_location_text)
{
    network_diag_set_line(kNetworkDiagLocalIpLine, kNetworkDiagLocalIpPlaceholder);
    network_diag_set_line(kNetworkDiagPublicIpLine, kNetworkDiagPublicIpPlaceholder);
    network_diag_set_line(kNetworkDiagIpLocationLine, ip_location_text);
    network_diag_set_line(kNetworkDiagDnsLine, kNetworkDiagDnsUnchecked);
    network_diag_set_line(kNetworkDiagWeatherLine, kNetworkDiagWeatherUnchecked);
    network_diag_set_line(kNetworkDiagNtpLine, kNetworkDiagNtpUnchecked);
    network_diag_set_line(kNetworkDiagSayingLine, kNetworkDiagSayingUnchecked);
    network_diag_set_line(kNetworkDiagInternetLine, kNetworkDiagInternetUnchecked);
    network_diag_set_line(kNetworkDiagOtaLine, kNetworkDiagOtaSourceUnchecked);
}

void finish_offline_network_requests(const NetworkSyncRequestSnapshot &requests)
{
    if (wifi_radio_on_load() && !setup_portal_active_load()) {
        stop_wifi_radio(true);
    }
    finish_requested_manual_syncs(requests, kNetworkStatusOfflineModeEnabled, false);
    if (requests.diagnostics) {
        network_diag_begin();
        for (int i = 0; i < kNetworkDiagLineCount; ++i) {
            network_diag_set_line(i, kNetworkStatusOfflineModeEnabled);
        }
        network_diag_finish();
        finish_settings_sync(kSettingsSyncNetworkDiag, kNetworkStatusOfflineModeEnabled);
    }
    clear_network_request_bits();
}

void finish_unconfigured_network_requests(const NetworkSyncRequestSnapshot &requests)
{
    finish_requested_manual_syncs(requests, kNetworkStatusWifiNotConfigured, true);
    if (requests.provisioning) {
        xEventGroupClearBits(g_app_events, kProvisioningSyncBit);
    }
    if (requests.diagnostics) {
        network_diag_begin();
        set_network_diag_unavailable(kNetworkDiagIpLocationWifiNotConfigured);
        network_diag_finish();
        finish_network_diagnostics_sync();
    }
}

void finish_failed_sync_requests(const NetworkSyncRequestSnapshot &requests)
{
    if (requests.provisioning) {
        xEventGroupClearBits(g_app_events, kProvisioningSyncBit);
    }
    if (requests.manual_ntp) {
        finish_settings_sync_and_clear_bit(kSettingsSyncNtp,
                                           kNetworkSyncTimeFailed,
                                           kManualNtpSyncBit);
    }
    if (requests.manual_weather) {
        finish_settings_sync_and_clear_bit(kSettingsSyncWeather,
                                           kNetworkSyncWeatherFailed,
                                           kManualWeatherSyncBit);
    }
    if (requests.manual_saying) {
        finish_settings_sync_and_clear_bit(kSettingsSyncSaying,
                                           kNetworkSyncSayingFailed,
                                           kManualSayingSyncBit);
    }
}

void finish_successful_sync_requests(const NetworkSyncRequestSnapshot &requests,
                                     bool ntp_ok,
                                     bool weather_ok,
                                     bool saying_ok)
{
    if (requests.provisioning) {
        xEventGroupClearBits(g_app_events, kProvisioningSyncBit);
    }
    if (requests.manual_ntp) {
        finish_settings_sync_and_clear_bit(kSettingsSyncNtp,
                                           ntp_ok ? kNetworkSyncTimeComplete : kNetworkSyncTimeFailed,
                                           kManualNtpSyncBit);
    }
    if (requests.manual_weather) {
        finish_settings_sync_and_clear_bit(kSettingsSyncWeather,
                                           weather_ok ? kNetworkSyncWeatherComplete : kNetworkSyncWeatherFailed,
                                           kManualWeatherSyncBit);
        notify_ui_task();
    }
    if (requests.manual_saying) {
        finish_settings_sync_and_clear_bit(kSettingsSyncSaying,
                                           saying_ok ? kNetworkSyncSayingComplete : kNetworkSyncSayingFailed,
                                           kManualSayingSyncBit);
        notify_ui_task();
    }
}

void finish_network_diagnostics_sync()
{
    finish_settings_sync_and_clear_bit(kSettingsSyncNetworkDiag,
                                       kNetworkSyncNetworkDiagComplete,
                                       kNetworkDiagBit);
}
