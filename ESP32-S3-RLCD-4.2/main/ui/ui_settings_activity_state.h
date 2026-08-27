// 提供设置页请求、活动时间和确认动作序号的原子访问接口。
#pragma once

#include <stdint.h>

bool settings_page_requested();
void settings_page_request();
void settings_page_clear();
uint32_t settings_activity_action_sequence();
uint32_t settings_activity_last_tick();
void settings_activity_record(uint32_t tick);
void settings_activity_record_action(uint32_t tick);
