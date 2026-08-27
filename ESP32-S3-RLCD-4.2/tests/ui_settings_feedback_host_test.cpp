// 直接验证设置页反馈和手动同步超时收尾的生产实现。
#include "ui_settings_feedback.h"
#include "ui_settings_activity_state.h"
#include "ui_settings_sync_state.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

const char *const TAG = "Test";
EventGroupHandle_t g_app_events = reinterpret_cast<EventGroupHandle_t>(1);

namespace {
TickType_t s_now = 0;
EventBits_t s_event_bits = 0;
int s_notify_count = 0;
int s_network_diag_state = kNetworkDiagIdle;

SettingsSyncStateSnapshot sync_state()
{
    SettingsSyncStateSnapshot state;
    settings_sync_state_load(&state);
    return state;
}

void expect_active_feedback(TickType_t now, const char *expected)
{
    char feedback[kSettingsFeedbackTextLen] = {};
    assert(settings_feedback_copy_active(now, feedback, sizeof(feedback)));
    assert(strcmp(feedback, expected) == 0);
}

void reset_state()
{
    clear_settings_feedback();
    settings_page_request();
    settings_activity_record(0);
    settings_sync_state_begin(kSettingsSyncNone, 0);
    s_network_diag_state = kNetworkDiagIdle;
    s_now = 100;
    s_event_bits = kManualNtpSyncBit | kManualWeatherSyncBit | kManualSayingSyncBit;
    s_notify_count = 0;
}

void expect_timeout(SettingsSyncOp op, EventBits_t bit, const char *feedback)
{
    reset_state();
    settings_sync_state_begin(op, 120);
    assert(!finish_settings_sync_if_timed_out(119));
    assert(sync_state().operation == op);
    assert((s_event_bits & bit) != 0);

    s_now = 120;
    assert(finish_settings_sync_if_timed_out(120));
    assert(sync_state().operation == kSettingsSyncNone);
    assert(sync_state().deadline_tick == 0);
    assert((s_event_bits & bit) == 0);
    expect_active_feedback(120, feedback);
    assert(settings_activity_last_tick() == 120);
    assert(s_notify_count == 1);
}
} // namespace

int network_diag_state_load()
{
    return s_network_diag_state;
}

TickType_t xTaskGetTickCount()
{
    return s_now;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t bits)
{
    EventBits_t previous = s_event_bits;
    s_event_bits &= ~bits;
    return previous;
}

void notify_ui_task()
{
    ++s_notify_count;
}

extern "C" size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t length = strlen(src);
    if (size > 0) {
        size_t copy_length = length < size - 1 ? length : size - 1;
        memcpy(dst, src, copy_length);
        dst[copy_length] = '\0';
    }
    return length;
}

int main()
{
    reset_state();
    set_settings_feedback("测试", 2500);
    expect_active_feedback(2599, "测试");
    char expired_feedback[kSettingsFeedbackTextLen] = {};
    assert(!settings_feedback_copy_active(2600, expired_feedback, sizeof(expired_feedback)));
    assert(expired_feedback[0] == '\0');
    assert(settings_activity_last_tick() == 100);
    assert(s_notify_count == 1);

    reset_state();
    begin_settings_sync(kSettingsSyncWeather, "同步中");
    assert(sync_state().operation == kSettingsSyncWeather);
    assert(sync_state().deadline_tick == 100 + kSettingsManualSyncTimeoutMs);
    expect_active_feedback(100, "同步中");
    finish_settings_sync(kSettingsSyncNtp, "错误操作");
    assert(sync_state().operation == kSettingsSyncWeather);

    expect_timeout(kSettingsSyncNtp, kManualNtpSyncBit, "时间同步超时");
    expect_timeout(kSettingsSyncWeather, kManualWeatherSyncBit, "天气同步超时");
    expect_timeout(kSettingsSyncSaying, kManualSayingSyncBit, "一言更新超时");

    reset_state();
    settings_sync_state_begin(kSettingsSyncNtp, UINT32_MAX - 4);
    assert(!finish_settings_sync_if_timed_out(UINT32_MAX - 5));
    s_now = 1;
    assert(finish_settings_sync_if_timed_out(1));

    reset_state();
    settings_sync_state_begin(kSettingsSyncNetworkDiag, 100);
    assert(finish_settings_sync_if_timed_out(100));
    assert(sync_state().operation == kSettingsSyncNone);
    assert(sync_state().deadline_tick == 0);
    char feedback[kSettingsFeedbackTextLen] = {};
    assert(!settings_feedback_copy_active(100, feedback, sizeof(feedback)));
    assert(feedback[0] == '\0');
    assert(s_notify_count == 0);
    return 0;
}
