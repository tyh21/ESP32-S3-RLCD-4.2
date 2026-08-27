// 验证电池充电趋势状态机的进入、动画完成、回落退出和 Tick 回绕。
#include "battery_charging_state.h"

#include <assert.h>
#include <stdint.h>

namespace {
constexpr BatteryChargingPolicy kPolicy = {
    3.0f,
    0.035f,
    0.006f,
    1,
    5,
    96,
    600,
};

BatteryChargingInput input(float previous,
                           float current,
                           int percent,
                           uint32_t tick)
{
    return {previous, current, percent, tick};
}
} // namespace

int main()
{
    assert(!update_battery_charging_state(input(3.8f, 3.9f, 50, 0),
                                           kPolicy,
                                           nullptr,
                                           nullptr));

    BatteryChargingTracker tracker = {1, 2, 4.0f, true, 10};
    BatteryChargingState state = {false, true};
    assert(update_battery_charging_state(input(-1.0f, 3.8f, 50, 20),
                                         kPolicy,
                                         &tracker,
                                         &state));
    assert(!state.charging && !state.animation_complete);
    assert(tracker.rise_samples == 0 && tracker.stop_samples == 0);
    assert(tracker.peak_voltage == 0.0f && !tracker.peak_tick_set);

    tracker = {};
    state = {};
    assert(update_battery_charging_state(input(3.80f, 3.82f, 50, 100),
                                         kPolicy,
                                         &tracker,
                                         &state));
    assert(!state.charging && tracker.rise_samples == 0);
    assert(update_battery_charging_state(input(3.82f, 3.855f, 50, 200),
                                         kPolicy,
                                         &tracker,
                                         &state));
    assert(state.charging && !state.animation_complete);
    assert(tracker.peak_voltage == 3.855f && tracker.last_peak_tick == 200);

    assert(update_battery_charging_state(input(3.855f, 3.90f, 50, 300),
                                         kPolicy,
                                         &tracker,
                                         &state));
    assert(state.charging && tracker.peak_voltage == 3.90f);
    assert(tracker.last_peak_tick == 300 && tracker.stop_samples == 0);
    assert(update_battery_charging_state(input(3.90f, 3.90f, 96, 301),
                                         kPolicy,
                                         &tracker,
                                         &state));
    assert(state.charging && state.animation_complete);

    tracker = {};
    state = {};
    assert(update_battery_charging_state(input(3.80f, 3.84f, 50, UINT32_MAX - 300U),
                                         kPolicy,
                                         &tracker,
                                         &state));
    assert(state.charging && !state.animation_complete);
    assert(update_battery_charging_state(input(3.84f, 3.84f, 50, 298U),
                                         kPolicy,
                                         &tracker,
                                         &state));
    assert(state.charging && !state.animation_complete);
    assert(update_battery_charging_state(input(3.84f, 3.84f, 50, 299U),
                                         kPolicy,
                                         &tracker,
                                         &state));
    assert(state.charging && state.animation_complete);

    tracker = {};
    state = {};
    assert(update_battery_charging_state(input(3.80f, 3.84f, 50, 100),
                                         kPolicy,
                                         &tracker,
                                         &state));
    for (int i = 0; i < 4; ++i) {
        assert(update_battery_charging_state(input(3.84f, 3.833f, 50, 101 + i),
                                             kPolicy,
                                             &tracker,
                                             &state));
        assert(state.charging);
    }
    assert(tracker.stop_samples == 4);
    assert(update_battery_charging_state(input(3.84f, 3.833f, 50, 105),
                                         kPolicy,
                                         &tracker,
                                         &state));
    assert(!state.charging && !state.animation_complete);
    assert(tracker.rise_samples == 0 && tracker.stop_samples == 0);
    assert(tracker.peak_voltage == 0.0f && !tracker.peak_tick_set);
    return 0;
}
