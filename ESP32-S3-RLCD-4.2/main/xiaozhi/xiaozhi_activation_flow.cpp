// 处理小智激活配置恢复、首次绑定和激活响应应用。
#include "xiaozhi_activation_flow.h"

#include "scoped_heap_buffer.h"
#include "xiaozhi_activation_client.h"
#include "xiaozhi_activation_response_parser.h"
#include "xiaozhi_activation_storage.h"
#include "xiaozhi_binding_voice.h"
#include "xiaozhi_snapshot_state.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

namespace {

constexpr const char *kTag = "WeatherClock";
constexpr const char *kActivatingStatus = "正在连接小智服务";
constexpr const char *kBindingStatus = "请绑定设备";
constexpr const char *kReadyStatus = "等待唤醒词";
constexpr const char *kErrorStatus = "小智服务不可用";
constexpr const char *kBoundDetail = "说出唤醒词即可开始对话";
constexpr const char *kActivationFailureDetail = "稍后将自动重试";
constexpr const char *kBindingFallbackDetail = "请在小智服务中输入绑定 ID";

bool apply_activation_response(const XiaozhiActivationResponse &response)
{
    XiaozhiActivationResponseDocument document;
    if (!document.parse(response.data, response.len)) {
        return false;
    }
    const char *detail = document.message() ? document.message() : kBindingFallbackDetail;
    const char *binding_code = document.binding_code() ? document.binding_code() : "";
    const char *challenge_text = document.challenge() ? document.challenge() : "";
    // 未绑定响应可能同时携带 WebSocket 参数和绑定码。绑定码必须优先，
    // 否则把临时连接参数持久化后，下次启动会被误判成已经绑定。
    if (binding_code[0] != '\0') {
        xiaozhi_snapshot_set(kXiaozhiAiBinding,
                             kBindingStatus,
                             detail,
                             binding_code);
        xiaozhi_announce_binding_id_once(binding_code);
        ESP_LOGI(kTag, "xiaozhi device binding required");
        return true;
    }
    bool configured = xiaozhi_save_activation_config(document.websocket(),
                                                      challenge_text);
    if (configured) {
        xiaozhi_snapshot_set(kXiaozhiAiReady, kReadyStatus, kBoundDetail);
        ESP_LOGI(kTag, "xiaozhi device binding confirmed");
    }
    return configured;
}

} // namespace

void xiaozhi_activate_or_restore_session()
{
    char url[256] = {};
    char token[256] = {};
    int32_t version = 1;
    if (xiaozhi_load_websocket_config(url,
                                      sizeof(url),
                                      token,
                                      sizeof(token),
                                      &version)) {
        xiaozhi_snapshot_set(kXiaozhiAiReady, kReadyStatus, kBoundDetail);
        return;
    }
    xiaozhi_snapshot_set(kXiaozhiAiActivating,
                         kActivatingStatus,
                         "正在请求设备绑定信息");
    ScopedHeapBuffer<uint8_t> response_storage(
        static_cast<uint8_t *>(heap_caps_calloc(
            1,
            sizeof(XiaozhiActivationResponse),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        sizeof(XiaozhiActivationResponse));
    if (!response_storage) {
        xiaozhi_snapshot_set(kXiaozhiAiError,
                             kErrorStatus,
                             "小智内存不足，请稍后重试");
        return;
    }
    XiaozhiActivationResponse *response =
        reinterpret_cast<XiaozhiActivationResponse *>(response_storage.data());
    bool activated = xiaozhi_request_activation(response) &&
                     apply_activation_response(*response);
    response_storage.reset();
    if (!activated) {
        xiaozhi_snapshot_set(kXiaozhiAiError,
                             kErrorStatus,
                             kActivationFailureDetail);
    }
}
