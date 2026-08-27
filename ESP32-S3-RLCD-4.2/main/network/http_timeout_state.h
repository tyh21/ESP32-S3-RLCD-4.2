// 声明 HTTP 客户端默认超时的线程安全运行态接口。
#pragma once

int network_http_timeout_ms_load();
int network_http_timeout_ms_exchange(int timeout_ms);
void network_http_timeout_ms_store(int timeout_ms);
