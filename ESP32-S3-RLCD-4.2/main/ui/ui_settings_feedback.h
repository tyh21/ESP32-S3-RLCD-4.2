// 声明设置页反馈和手动网络同步状态的共享接口。
#pragma once

#include "app_state.h"

inline constexpr size_t kSettingsFeedbackTextLen = 48;

void set_settings_feedback(const char *text, uint32_t duration_ms);
void clear_settings_feedback();
bool settings_feedback_copy_active(TickType_t now, char *out, size_t out_len);
bool is_settings_sync_busy();
void begin_settings_sync(SettingsSyncOp op, const char *text);
void finish_settings_sync(SettingsSyncOp op, const char *text);
bool finish_settings_sync_if_timed_out(TickType_t now);
