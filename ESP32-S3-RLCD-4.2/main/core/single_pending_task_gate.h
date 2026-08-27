// 提供同类延后任务最多保留一个待处理实例的轻量原子门控。
#pragma once

#include <atomic>

class SinglePendingTaskGate {
public:
    bool try_acquire()
    {
        bool expected = false;
        return active_.compare_exchange_strong(expected,
                                               true,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire);
    }

    void release()
    {
        active_.store(false, std::memory_order_release);
    }

    bool active() const
    {
        return active_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> active_{false};
};
