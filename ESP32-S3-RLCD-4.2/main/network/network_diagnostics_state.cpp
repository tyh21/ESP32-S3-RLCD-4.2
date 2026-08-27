// 管理网络检测后台写入与 UI 读取之间的一致状态快照。
#include "network_diagnostics_state.h"

#include <freertos/FreeRTOS.h>

#include <cstring>

namespace {
portMUX_TYPE s_network_diag_mux = portMUX_INITIALIZER_UNLOCKED;
int s_network_diag_state = 0;
char s_network_diag_lines[kNetworkDiagLineCount][kNetworkDiagLineLen] = {};
bool s_network_diag_page_requested = false;

void copy_line(char *out, const char *text)
{
    memset(out, 0, kNetworkDiagLineLen);
    if (!text) {
        return;
    }
    size_t length = strnlen(text, kNetworkDiagLineLen - 1);
    memcpy(out, text, length);
}
} // namespace

void network_diag_state_clear(int state)
{
    portENTER_CRITICAL(&s_network_diag_mux);
    s_network_diag_state = state;
    memset(s_network_diag_lines, 0, sizeof(s_network_diag_lines));
    portEXIT_CRITICAL(&s_network_diag_mux);
}

void network_diag_state_store(int state)
{
    portENTER_CRITICAL(&s_network_diag_mux);
    s_network_diag_state = state;
    portEXIT_CRITICAL(&s_network_diag_mux);
}

int network_diag_state_load()
{
    portENTER_CRITICAL(&s_network_diag_mux);
    int state = s_network_diag_state;
    portEXIT_CRITICAL(&s_network_diag_mux);
    return state;
}

void network_diag_line_store(int index, const char *text)
{
    if (!network_diag_line_index_valid(index)) {
        return;
    }
    portENTER_CRITICAL(&s_network_diag_mux);
    copy_line(s_network_diag_lines[index], text);
    portEXIT_CRITICAL(&s_network_diag_mux);
}

void network_diag_snapshot_load(NetworkDiagnosticsSnapshot *snapshot)
{
    if (!snapshot) {
        return;
    }
    portENTER_CRITICAL(&s_network_diag_mux);
    snapshot->state = s_network_diag_state;
    memcpy(snapshot->lines, s_network_diag_lines, sizeof(snapshot->lines));
    portEXIT_CRITICAL(&s_network_diag_mux);
}

bool network_diag_page_requested()
{
    portENTER_CRITICAL(&s_network_diag_mux);
    bool requested = s_network_diag_page_requested;
    portEXIT_CRITICAL(&s_network_diag_mux);
    return requested;
}

void network_diag_page_request()
{
    portENTER_CRITICAL(&s_network_diag_mux);
    s_network_diag_page_requested = true;
    portEXIT_CRITICAL(&s_network_diag_mux);
}

void network_diag_page_clear()
{
    portENTER_CRITICAL(&s_network_diag_mux);
    s_network_diag_page_requested = false;
    portEXIT_CRITICAL(&s_network_diag_mux);
}
