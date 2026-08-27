// 验证联网任务唤醒锁、HTTP 事务锁和临时超时守卫的作用域恢复。
#include "network_task_guards.h"

#include <assert.h>

namespace {
int g_awake_acquire_calls = 0;
int g_awake_release_calls = 0;
int g_http_lock_acquire_calls = 0;
int g_http_lock_release_calls = 0;
TickType_t g_http_lock_last_timeout = 0;
bool g_http_lock_available = true;
bool g_awake_lock_available = true;
} // namespace

bool acquire_network_awake_lock()
{
    ++g_awake_acquire_calls;
    return g_awake_lock_available;
}

void release_network_awake_lock()
{
    ++g_awake_release_calls;
}

bool acquire_network_http_transaction_lock(TickType_t timeout)
{
    ++g_http_lock_acquire_calls;
    g_http_lock_last_timeout = timeout;
    return g_http_lock_available;
}

void release_network_http_transaction_lock()
{
    ++g_http_lock_release_calls;
}

int main()
{
    {
        NetworkAwakeLockGuard guard;
        assert(guard.locked());
        assert(g_awake_acquire_calls == 1);
        assert(g_awake_release_calls == 0);
    }
    assert(g_awake_release_calls == 1);

    {
        NetworkAwakeLockGuard guard;
        assert(guard.locked());
        assert(g_awake_acquire_calls == 2);
        guard.release();
        guard.release();
        assert(g_awake_release_calls == 2);
    }
    assert(g_awake_release_calls == 2);

    g_awake_lock_available = false;
    {
        NetworkAwakeLockGuard guard;
        assert(!guard.locked());
        guard.release();
        assert(g_awake_acquire_calls == 3);
        assert(g_awake_release_calls == 2);
    }
    assert(g_awake_release_calls == 2);

    {
        NetworkHttpTransactionGuard guard(321);
        assert(guard.locked());
        assert(g_http_lock_acquire_calls == 1);
        assert(g_http_lock_last_timeout == 321);
        assert(g_http_lock_release_calls == 0);
    }
    assert(g_http_lock_release_calls == 1);

    g_http_lock_available = false;
    {
        NetworkHttpTransactionGuard guard(654);
        assert(!guard.locked());
        assert(g_http_lock_acquire_calls == 2);
        assert(g_http_lock_last_timeout == 654);
    }
    assert(g_http_lock_release_calls == 1);

    assert(network_http_timeout_ms_load() == 10000);
    network_http_timeout_ms_store(5000);
    assert(network_http_timeout_ms_load() == 5000);
    {
        NetworkHttpTimeoutGuard outer(2500);
        assert(network_http_timeout_ms_load() == 2500);
        {
            NetworkHttpTimeoutGuard inner(750);
            assert(network_http_timeout_ms_load() == 750);
        }
        assert(network_http_timeout_ms_load() == 2500);
    }
    assert(network_http_timeout_ms_load() == 5000);
    return 0;
}
