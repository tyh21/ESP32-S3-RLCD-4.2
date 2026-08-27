// 发布只用于唤醒后台网络调度器的运行态变化事件。
#include "network_runtime_events.h"

#include "app_state.h"

void notify_network_sync_runtime_state_changed()
{
    if (g_app_events) {
        xEventGroupSetBits(g_app_events, kNetworkStateChangedBit);
    }
}
