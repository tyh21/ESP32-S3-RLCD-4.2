// 实现小智实时会话与待唤醒阶段的网络和电源所有权切换。
#include "xiaozhi_power_session.h"

#include "app_state.h"
#include "audio_services.h"
#include "network_services.h"
#include "sensor_services.h"
#include "wifi_radio_state.h"

#include <atomic>

#include "esp_wifi.h"

namespace {
constexpr uint32_t kWifiWaitMs = 30000;

std::atomic<bool> s_network_keepalive{false};
bool s_network_lock_held = false;
bool s_idle_low_power = false;
} // namespace

bool xiaozhi_power_session_keepalive_active()
{
    return s_network_keepalive.load(std::memory_order_acquire);
}

bool xiaozhi_power_session_task_start_blocked()
{
    return network_awake_lock_active();
}

XiaozhiPowerSessionSnapshot xiaozhi_power_session_snapshot()
{
    return {
        xiaozhi_power_session_keepalive_active(),
        s_network_lock_held,
        s_idle_low_power,
    };
}

void xiaozhi_power_session_release()
{
    s_idle_low_power = false;
    if (xiaozhi_power_session_keepalive_active()) {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        s_network_keepalive.store(false, std::memory_order_release);
    }
    if (s_network_lock_held) {
        release_network_awake_lock();
        s_network_lock_held = false;
    }
    // Other weather or diagnostics work may still own the shared network lock.
    // Defer radio shutdown until the final owner leaves instead of leaking Wi-Fi.
    request_wifi_radio_stop_when_idle();
    service_wifi_radio_stop_when_idle();
}

bool xiaozhi_power_session_set_idle(bool enabled)
{
    if (enabled == s_idle_low_power) {
        return true;
    }
    if (enabled) {
        if (s_network_lock_held) {
            release_network_awake_lock();
            s_network_lock_held = false;
        }
        esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
        if (ps_err != ESP_OK) {
            ESP_LOGW(TAG, "Xiaozhi idle Wi-Fi power save failed: %s", esp_err_to_name(ps_err));
        }
        set_xiaozhi_audio_high_performance(false);
        s_idle_low_power = true;
        ESP_LOGI(TAG, "Xiaozhi wake idle power: CPU DFS + Wi-Fi max modem sleep");
        return true;
    }
    if (!s_network_lock_held) {
        if (!acquire_network_awake_lock()) {
            ESP_LOGW(TAG, "Xiaozhi network PM lock unavailable");
            return false;
        }
        s_network_lock_held = true;
    }
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, "Xiaozhi realtime Wi-Fi power save disable failed: %s", esp_err_to_name(ps_err));
    }
    set_xiaozhi_audio_high_performance(true);
    s_idle_low_power = false;
    ESP_LOGI(TAG, "Xiaozhi realtime power restored");
    return true;
}

bool xiaozhi_power_session_acquire_realtime()
{
    bool connected = g_app_events &&
                     ((xEventGroupGetBits(g_app_events) & kWifiConnectedBit) != 0);
    if (s_idle_low_power && wifi_radio_on_load() && connected) {
        return true;
    }
    if (s_idle_low_power && !xiaozhi_power_session_set_idle(false)) {
        return false;
    }
    if (!s_network_lock_held) {
        if (!acquire_network_awake_lock()) {
            ESP_LOGW(TAG, "Xiaozhi network PM lock unavailable");
            return false;
        }
        s_network_lock_held = true;
    }
    if (!wifi_radio_on_load() && !start_wifi_radio(false)) {
        return false;
    }
    // Once the page owns keepalive, only publish the transition once. Repeating
    // esp_wifi_set_ps() produces noisy logs and unnecessary driver work.
    if (!xiaozhi_power_session_keepalive_active()) {
        s_network_keepalive.store(true, std::memory_order_release);
        if (esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
            ESP_LOGW(TAG, "Xiaozhi Wi-Fi power save disable failed");
        }
    }
    return wait_for_wifi_connected(kWifiWaitMs);
}
