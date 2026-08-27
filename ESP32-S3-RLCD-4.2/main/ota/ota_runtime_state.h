// 提供 OTA 任务与 UI/网络消费者之间的一致运行态快照。
#pragma once

#include "ota_flow_policy.h"

#include <freertos/FreeRTOS.h>

inline constexpr int kOtaStatusLen = 96;

struct OtaRuntimeSnapshot {
    int state = kOtaIdle;
    int progress = -1;
    int speed_kbps = -1;
    TickType_t status_until_tick = 0;
    bool status_hold_set = false;
    bool reboot_pending = false;
    char status[kOtaStatusLen] = {};
};

void ota_runtime_snapshot_load(OtaRuntimeSnapshot *snapshot);
int ota_runtime_state_load();
bool ota_runtime_reboot_pending_load();
void ota_runtime_publish_status(int state,
                                const char *status,
                                int progress,
                                TickType_t status_until_tick,
                                bool status_hold_set);
void ota_runtime_publish_download_status(const char *status,
                                         int progress,
                                         int speed_kbps);
void ota_runtime_reboot_pending_store(bool pending);
void ota_runtime_reset_status_if_idle(TickType_t now, const char *idle_status);
