// 实现小智状态快照的单一互斥访问边界。
#include "xiaozhi_snapshot_state.h"

#include "ui_task_notify.h"
#include "xiaozhi_snapshot_change.h"
#include "xiaozhi_text_utils.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string.h>

namespace {
constexpr const char *kNeutralEmotion = "neutral";

SemaphoreHandle_t s_snapshot_mutex = nullptr;
// 固定 char 数组的静态聚合初始化需要字符串字面量。
XiaozhiAiSnapshot s_snapshot = {
    kXiaozhiAiInactive, "小智准备中", "", "", "neutral", 0, 0};

void begin_state_update_locked(XiaozhiAiState state, const char *status)
{
    s_snapshot.state = state;
    xiaozhi_protocol::utf8_safe_copy(s_snapshot.status,
                                     sizeof(s_snapshot.status),
                                     status);
}

void finish_state_update_locked(XiaozhiAiState state)
{
    if (state != kXiaozhiAiSpeaking) {
        strlcpy(s_snapshot.emotion, kNeutralEmotion, sizeof(s_snapshot.emotion));
    }
    s_snapshot.waveform_level =
        state == kXiaozhiAiListening || state == kXiaozhiAiSpeaking ? 1 : 0;
}
} // namespace

bool xiaozhi_snapshot_state_init()
{
    if (s_snapshot_mutex) {
        return true;
    }
    s_snapshot_mutex = xSemaphoreCreateMutex();
    return s_snapshot_mutex != nullptr;
}

void xiaozhi_snapshot_state_deinit()
{
    if (!s_snapshot_mutex) {
        return;
    }
    vSemaphoreDelete(s_snapshot_mutex);
    s_snapshot_mutex = nullptr;
}

void xiaozhi_snapshot_set(XiaozhiAiState state,
                          const char *status,
                          const char *detail,
                          const char *binding_code)
{
    if (!s_snapshot_mutex ||
        xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    const XiaozhiAiSnapshot before = s_snapshot;
    begin_state_update_locked(state, status);
    xiaozhi_protocol::utf8_safe_copy(s_snapshot.detail,
                                     sizeof(s_snapshot.detail),
                                     detail);
    xiaozhi_protocol::utf8_safe_copy(s_snapshot.binding_code,
                                     sizeof(s_snapshot.binding_code),
                                     binding_code);
    finish_state_update_locked(state);
    const bool changed = !xiaozhi_snapshot_content_equal(before, s_snapshot);
    xSemaphoreGive(s_snapshot_mutex);
    if (changed) {
        notify_ui_task();
    }
}

void xiaozhi_snapshot_set_status_preserving_detail(XiaozhiAiState state,
                                                    const char *status)
{
    if (!s_snapshot_mutex ||
        xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    const XiaozhiAiSnapshot before = s_snapshot;
    begin_state_update_locked(state, status);
    finish_state_update_locked(state);
    const bool changed = !xiaozhi_snapshot_content_equal(before, s_snapshot);
    xSemaphoreGive(s_snapshot_mutex);
    if (changed) {
        notify_ui_task();
    }
}

void xiaozhi_snapshot_set_emotion(const char *emotion)
{
    if (!emotion || !s_snapshot_mutex ||
        xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    const XiaozhiAiSnapshot before = s_snapshot;
    xiaozhi_protocol::utf8_safe_copy(s_snapshot.emotion,
                                     sizeof(s_snapshot.emotion),
                                     emotion);
    const bool changed = !xiaozhi_snapshot_content_equal(before, s_snapshot);
    xSemaphoreGive(s_snapshot_mutex);
    if (changed) {
        notify_ui_task();
    }
}

void xiaozhi_snapshot_mark_user_activity()
{
    if (!s_snapshot_mutex ||
        xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    ++s_snapshot.activity_sequence;
    xSemaphoreGive(s_snapshot_mutex);
    notify_ui_task();
}

void xiaozhi_snapshot_get(XiaozhiAiSnapshot *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!s_snapshot_mutex ||
        xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        out->state = kXiaozhiAiInactive;
        strlcpy(out->status, kXiaozhiDefaultStatus, sizeof(out->status));
        return;
    }
    *out = s_snapshot;
    xSemaphoreGive(s_snapshot_mutex);
}
