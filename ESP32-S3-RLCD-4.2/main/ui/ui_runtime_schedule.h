// 声明 UI 运行期超时、辅助页门控、唤醒调度和小智自动返回入口。
#pragma once

#include <freertos/FreeRTOS.h>

#include <stdint.h>
#include <time.h>

bool ui_runtime_settings_timeout_elapsed(TickType_t last_activity);
bool ui_runtime_auxiliary_page_requested();
TickType_t ui_runtime_next_loop_delay_ticks(const struct tm &local,
                                            bool battery_blink_visible);
void ui_runtime_update_xiaozhi_auto_return(TickType_t tick_now,
                                           TickType_t &last_activity_tick,
                                           uint32_t &last_activity_sequence);
