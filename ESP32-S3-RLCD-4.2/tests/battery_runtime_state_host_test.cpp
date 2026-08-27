// 验证电池运行态生产模块的一致快照和低电量迟滞规则。
#include "battery_runtime_state.h"

#include <atomic>
#include <cassert>
#include <thread>

namespace {
constexpr int kIterations = 50000;
constexpr int kEnterPercent = 10;
constexpr int kExitPercent = 13;
}

int main()
{
    BatteryRuntimeSnapshot snapshot;
    battery_runtime_snapshot_load(&snapshot);
    assert(snapshot.percent == -1);
    assert(snapshot.voltage == -1.0f);
    assert(!snapshot.charging);
    assert(!snapshot.low_battery_mode);

    assert(!battery_low_mode_for_percent(false, -1, kEnterPercent, kExitPercent));
    assert(battery_low_mode_for_percent(false, 9, kEnterPercent, kExitPercent));
    assert(battery_low_mode_for_percent(true, 12, kEnterPercent, kExitPercent));
    assert(!battery_low_mode_for_percent(true, 13, kEnterPercent, kExitPercent));

    snapshot.percent = 9;
    battery_runtime_snapshot_store(snapshot);
    assert(battery_runtime_low_mode_update(kEnterPercent, kExitPercent));
    assert(battery_low_mode_load());
    assert(!battery_runtime_low_mode_update(kEnterPercent, kExitPercent));
    snapshot.percent = 13;
    snapshot.low_battery_mode = true;
    battery_runtime_snapshot_store(snapshot);
    assert(battery_runtime_low_mode_update(kEnterPercent, kExitPercent));
    assert(!battery_low_mode_load());

    snapshot.percent = 50;
    snapshot.voltage = 50.0f;
    snapshot.charging = true;
    snapshot.animation_complete = true;
    snapshot.last_charge_time = 1234;
    snapshot.version = 7;
    snapshot.low_battery_mode = false;
    battery_runtime_snapshot_store(snapshot);
    assert(battery_percent_load() == 50);
    assert(battery_charging_load());
    assert(!battery_low_mode_load());

    battery_runtime_voltage_store(51.0f);
    battery_runtime_snapshot_load(&snapshot);
    assert(snapshot.voltage == 51.0f);
    assert(snapshot.percent == 50);
    assert(snapshot.version == 7);
    snapshot.voltage = 50.0f;
    snapshot.charging = false;
    snapshot.animation_complete = false;
    snapshot.last_charge_time = 50;
    battery_runtime_snapshot_store(snapshot);

    std::atomic<bool> inconsistent{false};
    std::thread writer([] {
        for (int i = 0; i < kIterations; ++i) {
            BatteryRuntimeSnapshot next;
            next.percent = i % 101;
            next.voltage = static_cast<float>(next.percent);
            next.charging = (next.percent % 2) != 0;
            next.animation_complete = next.charging;
            next.last_charge_time = next.percent;
            next.version = static_cast<uint32_t>(i);
            next.low_battery_mode = next.percent < kEnterPercent;
            battery_runtime_snapshot_store(next);
        }
    });
    std::thread reader([&] {
        for (int i = 0; i < kIterations; ++i) {
            BatteryRuntimeSnapshot current;
            battery_runtime_snapshot_load(&current);
            if (current.voltage != static_cast<float>(current.percent) ||
                current.animation_complete != current.charging ||
                current.last_charge_time != current.percent) {
                inconsistent.store(true, std::memory_order_relaxed);
            }
        }
    });
    writer.join();
    reader.join();
    assert(!inconsistent.load(std::memory_order_relaxed));
    return 0;
}
