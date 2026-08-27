// 验证 OTA 活跃状态、提示到期和 Tick 回绕边界。
#include "ota_flow_policy.h"

#include <assert.h>
#include <stdint.h>

int main()
{
    using Tick = uint32_t;
    constexpr Tick kNow = 100;
    constexpr Tick kPendingDeadline = 120;
    constexpr Tick kReachedDeadline = 100;

    assert(!ota_blocks_background_network_sync(kOtaIdle));
    assert(ota_blocks_background_network_sync(kOtaChecking));
    assert(!ota_blocks_background_network_sync(kOtaAvailable));
    assert(ota_blocks_background_network_sync(kOtaUpdating));
    assert(!ota_blocks_background_network_sync(kOtaSucceeded));
    assert(!ota_background_network_block_changed(kOtaChecking, kOtaUpdating));
    assert(ota_background_network_block_changed(kOtaIdle, kOtaChecking));
    assert(ota_background_network_block_changed(kOtaAvailable, kOtaUpdating));
    assert(ota_background_network_block_changed(kOtaUpdating, kOtaFailed));
    assert(!ota_background_network_block_changed(kOtaFailed, kOtaIdle));

    assert(ota_status_hold_active_for_tick(true, kNow, kPendingDeadline));
    assert(!ota_status_hold_active_for_tick(false, kNow, kPendingDeadline));
    assert(!ota_status_hold_active_for_tick(true, kNow, kReachedDeadline));

    assert(!ota_flow_active_for_tick(kOtaIdle, false, kNow, kPendingDeadline));
    assert(ota_flow_active_for_tick(kOtaChecking, false, kNow, kReachedDeadline));
    assert(ota_flow_active_for_tick(kOtaAvailable, true, kNow, kPendingDeadline));
    assert(!ota_flow_active_for_tick(kOtaAvailable, true, kNow, kReachedDeadline));
    assert(ota_flow_active_for_tick(kOtaUpdating, false, kNow, kReachedDeadline));
    assert(ota_flow_active_for_tick(kOtaSucceeded, true, kNow, kPendingDeadline));
    assert(!ota_flow_active_for_tick(kOtaSucceeded, true, kNow, kReachedDeadline));
    assert(!ota_flow_active_for_tick(kOtaFailed, true, kNow, kPendingDeadline));
    assert(!ota_flow_active_for_tick(kOtaNoUpdate, true, kNow, kPendingDeadline));

    assert(!ota_status_should_reset_to_idle(kOtaIdle, true, kNow, kReachedDeadline));
    assert(!ota_status_should_reset_to_idle(kOtaChecking, true, kNow, kReachedDeadline));
    assert(!ota_status_should_reset_to_idle(kOtaUpdating, true, kNow, kReachedDeadline));
    assert(!ota_status_should_reset_to_idle(kOtaFailed, false, kNow, kReachedDeadline));
    assert(!ota_status_should_reset_to_idle(kOtaAvailable, true, kNow, kPendingDeadline));
    assert(ota_status_should_reset_to_idle(kOtaAvailable, true, kNow, kReachedDeadline));
    assert(ota_status_should_reset_to_idle(kOtaSucceeded, true, kNow, kReachedDeadline));
    assert(ota_status_should_reset_to_idle(kOtaFailed, true, kNow, kReachedDeadline));
    assert(ota_status_should_reset_to_idle(kOtaNoUpdate, true, kNow, kReachedDeadline));

    constexpr Tick kWrapNow = UINT32_MAX - 5;
    constexpr Tick kWrapDeadline = 4;
    assert(ota_flow_active_for_tick(kOtaAvailable, true, kWrapNow, kWrapDeadline));
    assert(!ota_status_should_reset_to_idle(kOtaAvailable,
                                            true,
                                            kWrapNow,
                                            kWrapDeadline));
    assert(!ota_flow_active_for_tick(kOtaAvailable, true, 4U, kWrapDeadline));
    assert(ota_status_should_reset_to_idle(kOtaAvailable, true, 4U, kWrapDeadline));
    return 0;
}
