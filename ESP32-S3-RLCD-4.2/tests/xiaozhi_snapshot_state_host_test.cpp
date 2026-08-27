// 直接验证生产快照存储只在最终内容变化时通知 UI。
#include "xiaozhi_snapshot_state.h"

#include "freertos/semphr.h"
#include "ui_task_notify.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace {
int s_notify_count = 0;
int s_mutex_token = 0;
} // namespace

SemaphoreHandle_t xSemaphoreCreateMutex()
{
    return &s_mutex_token;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t)
{
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t)
{
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t)
{
}

void notify_ui_task()
{
    ++s_notify_count;
}

void register_ui_task_handle(TaskHandle_t)
{
}

int main()
{
    assert(xiaozhi_snapshot_state_init());

    xiaozhi_snapshot_set(kXiaozhiAiReady, "等待唤醒词", "请说你好，小智", "123456");
    assert(s_notify_count == 1);
    xiaozhi_snapshot_set(kXiaozhiAiReady, "等待唤醒词", "请说你好，小智", "123456");
    assert(s_notify_count == 1);
    xiaozhi_snapshot_set_status_preserving_detail(kXiaozhiAiReady, "等待唤醒词");
    assert(s_notify_count == 1);

    xiaozhi_snapshot_set_emotion("happy");
    assert(s_notify_count == 2);
    xiaozhi_snapshot_set_emotion("happy");
    assert(s_notify_count == 2);

    // 非 Speaking 状态的完整写入仍执行原有情绪复位规则。
    xiaozhi_snapshot_set(kXiaozhiAiReady, "等待唤醒词", "请说你好，小智", "123456");
    assert(s_notify_count == 3);
    xiaozhi_snapshot_set(kXiaozhiAiReady, "等待唤醒词", "请说你好，小智", "123456");
    assert(s_notify_count == 3);

    xiaozhi_snapshot_mark_user_activity();
    xiaozhi_snapshot_mark_user_activity();
    assert(s_notify_count == 5);

    XiaozhiAiSnapshot snapshot = {};
    xiaozhi_snapshot_get(&snapshot);
    assert(snapshot.state == kXiaozhiAiReady);
    assert(strcmp(snapshot.emotion, "neutral") == 0);
    assert(snapshot.waveform_level == 0);
    assert(snapshot.activity_sequence == 2);

    xiaozhi_snapshot_state_deinit();
    xiaozhi_snapshot_set(kXiaozhiAiError, "error", "ignored");
    assert(s_notify_count == 5);
    puts("Xiaozhi snapshot state host tests passed");
    return 0;
}
