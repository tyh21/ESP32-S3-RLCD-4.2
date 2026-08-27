// 验证网络检测状态与逐行文本快照的边界和独立性。
#include "network_diagnostics_state.h"

#include <assert.h>
#include <atomic>
#include <string.h>
#include <thread>

int main()
{
    assert(!network_diag_page_requested());
    network_diag_page_request();
    assert(network_diag_page_requested());
    network_diag_page_clear();
    assert(!network_diag_page_requested());

    NetworkDiagnosticsSnapshot snapshot;
    network_diag_snapshot_load(nullptr);
    network_diag_snapshot_load(&snapshot);
    assert(snapshot.state == 0);
    for (int index = 0; index < kNetworkDiagLineCount; ++index) {
        assert(snapshot.lines[index][0] == '\0');
    }

    network_diag_state_store(1);
    network_diag_line_store(kNetworkDiagLocalIpLine, "本地IP: 192.168.1.10");
    network_diag_line_store(-1, "ignored");
    network_diag_line_store(kNetworkDiagLineCount, "ignored");
    network_diag_snapshot_load(&snapshot);
    assert(snapshot.state == 1);
    assert(strcmp(snapshot.lines[kNetworkDiagLocalIpLine],
                  "本地IP: 192.168.1.10") == 0);

    char long_text[kNetworkDiagLineLen + 16];
    memset(long_text, 'x', sizeof(long_text));
    long_text[sizeof(long_text) - 1] = '\0';
    network_diag_line_store(kNetworkDiagOtaLine, long_text);
    NetworkDiagnosticsSnapshot truncated;
    network_diag_snapshot_load(&truncated);
    assert(strlen(truncated.lines[kNetworkDiagOtaLine]) ==
           kNetworkDiagLineLen - 1);

    network_diag_line_store(kNetworkDiagLocalIpLine, "updated");
    assert(strcmp(snapshot.lines[kNetworkDiagLocalIpLine],
                  "本地IP: 192.168.1.10") == 0);

    network_diag_line_store(kNetworkDiagLocalIpLine, nullptr);
    network_diag_state_clear(2);
    network_diag_snapshot_load(&snapshot);
    assert(snapshot.state == 2);
    for (int index = 0; index < kNetworkDiagLineCount; ++index) {
        assert(snapshot.lines[index][0] == '\0');
    }
    assert(network_diag_state_load() == 2);

    std::atomic<bool> start{false};
    std::thread writer([&start]() {
        while (!start.load(std::memory_order_acquire)) {
        }
        for (int iteration = 0; iteration < 1000; ++iteration) {
            network_diag_page_request();
            network_diag_page_clear();
        }
    });
    std::thread reader([&start]() {
        start.store(true, std::memory_order_release);
        for (int iteration = 0; iteration < 1000; ++iteration) {
            (void)network_diag_page_requested();
        }
    });
    writer.join();
    reader.join();
    assert(!network_diag_page_requested());
    return 0;
}
