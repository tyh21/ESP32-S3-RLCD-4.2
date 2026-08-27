// 提供网络检测页面请求、运行状态与逐行文本的线程安全接口。
#pragma once

#include "network_diagnostics_catalog.h"

struct NetworkDiagnosticsSnapshot {
    int state = 0;
    char lines[kNetworkDiagLineCount][kNetworkDiagLineLen] = {};
};

void network_diag_state_clear(int state);
void network_diag_state_store(int state);
int network_diag_state_load();
void network_diag_line_store(int index, const char *text);
void network_diag_snapshot_load(NetworkDiagnosticsSnapshot *snapshot);
bool network_diag_page_requested();
void network_diag_page_request();
void network_diag_page_clear();
