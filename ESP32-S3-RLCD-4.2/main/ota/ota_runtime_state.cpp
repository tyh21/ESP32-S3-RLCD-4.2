// 管理 OTA 状态、进度、速度和提示文本的短临界区存储。
#include "ota_runtime_state.h"

#include "network_runtime_events.h"
#include "ota_flow_policy.h"

#include <cstring>

namespace {
portMUX_TYPE s_ota_runtime_mux = portMUX_INITIALIZER_UNLOCKED;
OtaRuntimeSnapshot s_ota_runtime = {
    kOtaIdle,
    -1,
    -1,
    0,
    false,
    false,
    "BOOT: Check Update",
};

void copy_status(char *out, const char *status)
{
    memset(out, 0, kOtaStatusLen);
    if (!status) {
        return;
    }
    size_t length = strnlen(status, kOtaStatusLen - 1);
    memcpy(out, status, length);
}

void notify_background_network_block_changed(int previous_state,
                                             int current_state)
{
    if (ota_background_network_block_changed(previous_state, current_state)) {
        notify_network_sync_runtime_state_changed();
    }
}
} // namespace

void ota_runtime_snapshot_load(OtaRuntimeSnapshot *snapshot)
{
    if (!snapshot) {
        return;
    }
    portENTER_CRITICAL(&s_ota_runtime_mux);
    *snapshot = s_ota_runtime;
    portEXIT_CRITICAL(&s_ota_runtime_mux);
}

int ota_runtime_state_load()
{
    portENTER_CRITICAL(&s_ota_runtime_mux);
    int state = s_ota_runtime.state;
    portEXIT_CRITICAL(&s_ota_runtime_mux);
    return state;
}

bool ota_runtime_reboot_pending_load()
{
    portENTER_CRITICAL(&s_ota_runtime_mux);
    bool pending = s_ota_runtime.reboot_pending;
    portEXIT_CRITICAL(&s_ota_runtime_mux);
    return pending;
}

void ota_runtime_publish_status(int state,
                                const char *status,
                                int progress,
                                TickType_t status_until_tick,
                                bool status_hold_set)
{
    int previous_state = kOtaIdle;
    portENTER_CRITICAL(&s_ota_runtime_mux);
    previous_state = s_ota_runtime.state;
    s_ota_runtime.reboot_pending = false;
    s_ota_runtime.state = state;
    s_ota_runtime.progress = progress;
    copy_status(s_ota_runtime.status, status);
    s_ota_runtime.status_hold_set = status_hold_set;
    s_ota_runtime.status_until_tick = status_hold_set ? status_until_tick : 0;
    portEXIT_CRITICAL(&s_ota_runtime_mux);
    notify_background_network_block_changed(previous_state, state);
}

void ota_runtime_publish_download_status(const char *status,
                                         int progress,
                                         int speed_kbps)
{
    int previous_state = kOtaIdle;
    portENTER_CRITICAL(&s_ota_runtime_mux);
    previous_state = s_ota_runtime.state;
    s_ota_runtime.reboot_pending = false;
    s_ota_runtime.state = kOtaUpdating;
    s_ota_runtime.progress = progress;
    s_ota_runtime.speed_kbps = speed_kbps;
    copy_status(s_ota_runtime.status, status);
    s_ota_runtime.status_hold_set = false;
    s_ota_runtime.status_until_tick = 0;
    portEXIT_CRITICAL(&s_ota_runtime_mux);
    notify_background_network_block_changed(previous_state, kOtaUpdating);
}

void ota_runtime_reboot_pending_store(bool pending)
{
    portENTER_CRITICAL(&s_ota_runtime_mux);
    s_ota_runtime.reboot_pending = pending;
    portEXIT_CRITICAL(&s_ota_runtime_mux);
}

void ota_runtime_reset_status_if_idle(TickType_t now, const char *idle_status)
{
    int previous_state = kOtaIdle;
    int current_state = kOtaIdle;
    portENTER_CRITICAL(&s_ota_runtime_mux);
    previous_state = s_ota_runtime.state;
    if (ota_status_should_reset_to_idle(s_ota_runtime.state,
                                        s_ota_runtime.status_hold_set,
                                        now,
                                        s_ota_runtime.status_until_tick)) {
        s_ota_runtime.state = kOtaIdle;
        s_ota_runtime.status_hold_set = false;
        s_ota_runtime.status_until_tick = 0;
    }
    if (s_ota_runtime.state == kOtaIdle) {
        copy_status(s_ota_runtime.status, idle_status);
        s_ota_runtime.progress = -1;
        s_ota_runtime.speed_kbps = -1;
    }
    current_state = s_ota_runtime.state;
    portEXIT_CRITICAL(&s_ota_runtime_mux);
    notify_background_network_block_changed(previous_state, current_state);
}
