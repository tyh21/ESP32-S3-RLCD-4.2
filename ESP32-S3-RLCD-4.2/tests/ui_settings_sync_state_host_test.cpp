// 验证设置页同步操作与截止 Tick 在并发读写下保持一致快照。
#include "ui_settings_sync_state.h"

#include <atomic>
#include <cassert>
#include <thread>

namespace {
constexpr int kWeatherOperation = 2;
constexpr int kSayingOperation = 3;
constexpr uint32_t kWeatherDeadline = 120;
constexpr uint32_t kSayingDeadline = 240;
constexpr int kIterations = 100000;

bool snapshot_valid(const SettingsSyncStateSnapshot &state)
{
    return (state.operation == kWeatherOperation && state.deadline_tick == kWeatherDeadline) ||
           (state.operation == kSayingOperation && state.deadline_tick == kSayingDeadline);
}
}

int main()
{
    SettingsSyncStateSnapshot state;
    settings_sync_state_load(&state);
    assert(state.operation == 0);
    assert(state.deadline_tick == 0);

    settings_sync_state_begin(kWeatherOperation, kWeatherDeadline);
    assert(!settings_sync_state_clear_if(kSayingOperation));
    settings_sync_state_load(&state);
    assert(state.operation == kWeatherOperation);
    assert(state.deadline_tick == kWeatherDeadline);

    std::atomic<bool> invalid_snapshot_seen{false};
    std::thread writer([] {
        for (int i = 0; i < kIterations; ++i) {
            if ((i & 1) == 0) {
                settings_sync_state_begin(kWeatherOperation, kWeatherDeadline);
            } else {
                settings_sync_state_begin(kSayingOperation, kSayingDeadline);
            }
        }
    });
    std::thread reader([&] {
        for (int i = 0; i < kIterations; ++i) {
            SettingsSyncStateSnapshot snapshot;
            settings_sync_state_load(&snapshot);
            if (!snapshot_valid(snapshot)) {
                invalid_snapshot_seen.store(true, std::memory_order_relaxed);
            }
        }
    });
    writer.join();
    reader.join();

    assert(!invalid_snapshot_seen.load(std::memory_order_relaxed));
    settings_sync_state_load(&state);
    assert(settings_sync_state_clear_if(state.operation));
    settings_sync_state_load(&state);
    assert(state.operation == 0);
    assert(state.deadline_tick == 0);
    return 0;
}
