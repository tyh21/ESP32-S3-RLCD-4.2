// 声明设置页手动同步操作与截止 Tick 的一致快照接口。
#pragma once

#include <stdint.h>

struct SettingsSyncStateSnapshot {
    int operation = 0;
    uint32_t deadline_tick = 0;
};

void settings_sync_state_load(SettingsSyncStateSnapshot *out);
void settings_sync_state_begin(int operation, uint32_t deadline_tick);
bool settings_sync_state_clear_if(int operation);
