// 为联网任务提供可重复释放的唤醒锁、HTTP 事务锁和临时超时作用域守卫。
#pragma once

#include "app_state.h"
#include "http_timeout_state.h"
#include "sensor_services.h"

bool acquire_network_http_transaction_lock(TickType_t timeout);
void release_network_http_transaction_lock();

class NetworkAwakeLockGuard {
public:
    NetworkAwakeLockGuard()
        : active_(acquire_network_awake_lock())
    {
    }

    ~NetworkAwakeLockGuard()
    {
        release();
    }

    NetworkAwakeLockGuard(const NetworkAwakeLockGuard &) = delete;
    NetworkAwakeLockGuard &operator=(const NetworkAwakeLockGuard &) = delete;

    bool locked() const
    {
        return active_;
    }

    void release()
    {
        if (active_) {
            release_network_awake_lock();
            active_ = false;
        }
    }

private:
    bool active_ = false;
};

class NetworkHttpTransactionGuard {
public:
    explicit NetworkHttpTransactionGuard(TickType_t timeout)
        : locked_(acquire_network_http_transaction_lock(timeout))
    {
    }

    ~NetworkHttpTransactionGuard()
    {
        if (locked_) {
            release_network_http_transaction_lock();
        }
    }

    bool locked() const
    {
        return locked_;
    }

    NetworkHttpTransactionGuard(const NetworkHttpTransactionGuard &) = delete;
    NetworkHttpTransactionGuard &operator=(const NetworkHttpTransactionGuard &) = delete;

private:
    bool locked_ = false;
};

class NetworkHttpTimeoutGuard {
public:
    explicit NetworkHttpTimeoutGuard(int timeout_ms)
        : previous_timeout_ms_(network_http_timeout_ms_exchange(timeout_ms))
    {
    }

    ~NetworkHttpTimeoutGuard()
    {
        network_http_timeout_ms_store(previous_timeout_ms_);
    }

    NetworkHttpTimeoutGuard(const NetworkHttpTimeoutGuard &) = delete;
    NetworkHttpTimeoutGuard &operator=(const NetworkHttpTimeoutGuard &) = delete;

private:
    int previous_timeout_ms_ = 0;
};
