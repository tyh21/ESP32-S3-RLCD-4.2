// 验证小智退出后的 Wi-Fi 延后关闭不会抢占仍在运行的联网服务。
#include "wifi_idle_stop_policy.h"

#include <assert.h>

int main()
{
    WifiIdleStopPolicyInput input = {};
    assert(!wifi_idle_stop_allowed(input));

    input.requested = true;
    input.radio_on = true;
    assert(wifi_idle_stop_allowed(input));

    input.network_lock_active = true;
    assert(!wifi_idle_stop_allowed(input));
    input.network_lock_active = false;
    assert(wifi_idle_stop_allowed(input));

    input.xiaozhi_keepalive_active = true;
    assert(!wifi_idle_stop_allowed(input));
    input.xiaozhi_keepalive_active = false;

    input.setup_portal_active = true;
    assert(!wifi_idle_stop_allowed(input));
    input.setup_portal_active = false;

    input.ota_active = true;
    assert(!wifi_idle_stop_allowed(input));
    input.ota_active = false;
    assert(wifi_idle_stop_allowed(input));

    input.radio_on = false;
    assert(!wifi_idle_stop_allowed(input));
    return 0;
}
