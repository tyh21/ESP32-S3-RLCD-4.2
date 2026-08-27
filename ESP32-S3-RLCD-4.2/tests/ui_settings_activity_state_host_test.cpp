// 验证设置页活动状态在并发发布和读取时保持动作与时间的先后关系。
#include "ui_settings_activity_state.h"

#include <atomic>
#include <cassert>
#include <thread>

namespace {
constexpr uint32_t kActionCount = 100000;
}

int main()
{
    assert(!settings_page_requested());
    settings_page_request();
    assert(settings_page_requested());
    settings_page_clear();
    assert(!settings_page_requested());

    assert(settings_activity_action_sequence() == 0);
    assert(settings_activity_last_tick() == 0);

    settings_activity_record(17);
    assert(settings_activity_last_tick() == 17);

    std::atomic<bool> invalid_order_seen{false};
    std::thread writer([] {
        for (uint32_t i = 1; i <= kActionCount; ++i) {
            settings_activity_record_action(i + 17);
        }
    });
    std::thread reader([&] {
        uint32_t observed_sequence = 0;
        while (observed_sequence < kActionCount) {
            uint32_t sequence = settings_activity_action_sequence();
            if (sequence != observed_sequence) {
                uint32_t activity_tick = settings_activity_last_tick();
                if (activity_tick < sequence + 17) {
                    invalid_order_seen.store(true, std::memory_order_relaxed);
                }
                observed_sequence = sequence;
            }
        }
    });
    writer.join();
    reader.join();

    assert(!invalid_order_seen.load(std::memory_order_relaxed));
    assert(settings_activity_action_sequence() == kActionCount);
    assert(settings_activity_last_tick() == kActionCount + 17);

    std::atomic<bool> request_test_started{false};
    std::thread request_writer([&request_test_started] {
        request_test_started.store(true, std::memory_order_release);
        for (uint32_t i = 0; i < kActionCount; ++i) {
            settings_page_request();
            settings_page_clear();
        }
    });
    std::thread request_reader([&request_test_started] {
        while (!request_test_started.load(std::memory_order_acquire)) {
        }
        for (uint32_t i = 0; i < kActionCount; ++i) {
            (void)settings_page_requested();
        }
    });
    request_writer.join();
    request_reader.join();
    assert(!settings_page_requested());
    return 0;
}
