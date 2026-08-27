// 声明 HTTPS 期间复用的显示 DMA 保护和内存快照接口。
#pragma once

#include <stddef.h>

struct NetworkHttpsMemorySnapshot {
    size_t internal_free = 0;
    size_t internal_largest = 0;
    size_t dma_largest = 0;
    bool sufficient = false;
};

// TLS 握手期间切换到显示 DMA 保守模式，避免网络大块内存分配与 LCD
// 刷新争用内部 DMA 内存。天气、小智等联网模块复用同一个守卫。
class NetworkDisplayDmaGuard {
public:
    explicit NetworkDisplayDmaGuard(bool active);
    ~NetworkDisplayDmaGuard();

    NetworkDisplayDmaGuard(const NetworkDisplayDmaGuard &) = delete;
    NetworkDisplayDmaGuard &operator=(const NetworkDisplayDmaGuard &) = delete;

private:
    bool active_ = false;
};

// 启动 HTTPS 前统一采样内部堆和 DMA 最大连续块；调用方保留各自日志与延后策略。
NetworkHttpsMemorySnapshot capture_network_https_memory_snapshot();
