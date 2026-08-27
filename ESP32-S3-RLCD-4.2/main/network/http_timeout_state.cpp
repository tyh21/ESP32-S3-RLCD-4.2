// 集中维护 HTTP 客户端默认超时的跨任务原子状态。
#include "http_timeout_state.h"

#include <atomic>

namespace {
constexpr int kDefaultHttpTimeoutMs = 10000;
std::atomic<int> s_http_timeout_ms{kDefaultHttpTimeoutMs};

static_assert(kDefaultHttpTimeoutMs > 0, "default HTTP timeout must be positive");
} // namespace

int network_http_timeout_ms_load()
{
    return s_http_timeout_ms.load(std::memory_order_acquire);
}

int network_http_timeout_ms_exchange(int timeout_ms)
{
    return s_http_timeout_ms.exchange(timeout_ms, std::memory_order_acq_rel);
}

void network_http_timeout_ms_store(int timeout_ms)
{
    s_http_timeout_ms.store(timeout_ms, std::memory_order_release);
}
