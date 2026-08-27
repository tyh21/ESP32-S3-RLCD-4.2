#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 临时连接指定的 WiFi 路由器（STA 模式），通过 SNTP 获取网络时间并写入系统时钟，
 * 成功后自动断开 WiFi。用于时钟屏保期间的自动对时。
 *
 * 注意：web_gamepad 组件启动后设备处于 AP 模式。本组件会将 WiFi 切换到 STA，
 * 对时结束后切回 AP，因此不能在游戏/手柄页面使用期间调用（会短暂断开 AP）。
 */

typedef struct {
    bool synced;          /* 是否成功同步到时间 */
    int64_t elapsed_ms;   /* 整个过程耗时 */
} wifi_time_sync_result_t;

/* 执行一次自动对时：连接 WiFi -> NTP 同步 -> 断开。
 * 内部会切换 WiFi 模式 STA->(同步)->AP。 */
esp_err_t wifi_time_sync_once(const char *ssid, const char *password,
                              wifi_time_sync_result_t *result);

#ifdef __cplusplus
}
#endif