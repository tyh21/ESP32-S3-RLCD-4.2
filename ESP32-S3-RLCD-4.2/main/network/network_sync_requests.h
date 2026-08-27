// 声明网络同步请求快照、设置反馈和事件位收尾接口。
#pragma once

#include "app_state.h"

struct NetworkSyncRequestSnapshot {
    bool provisioning = false;
    bool manual_ntp = false;
    bool manual_weather = false;
    bool manual_saying = false;
    bool diagnostics = false;

    bool none_for_setup_portal() const
    {
        return !provisioning && !manual_ntp && !manual_weather &&
               !manual_saying && !diagnostics;
    }
};

NetworkSyncRequestSnapshot snapshot_network_sync_requests();
void finish_settings_sync_and_clear_bit(SettingsSyncOp op,
                                        const char *status,
                                        EventBits_t bit);
void set_network_diag_unavailable(const char *ip_location_text);
void finish_offline_network_requests(const NetworkSyncRequestSnapshot &requests);
void finish_unconfigured_network_requests(const NetworkSyncRequestSnapshot &requests);
void finish_failed_sync_requests(const NetworkSyncRequestSnapshot &requests);
void finish_successful_sync_requests(const NetworkSyncRequestSnapshot &requests,
                                     bool ntp_ok,
                                     bool weather_ok,
                                     bool saying_ok);
void finish_network_diagnostics_sync();
