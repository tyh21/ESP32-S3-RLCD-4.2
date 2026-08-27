// 管理 QWeather 动态响应内存、JSON 根对象和业务成功字段读取。
#include "qweather_response.h"

#include "app_constexpr.h"
#include "app_state.h"

#include <string.h>

namespace {
constexpr const char *kQweatherDefaultStage = "request";
constexpr const char *kQweatherJsonCodeField = "code";
constexpr const char *kQweatherSuccessCode = "200";
constexpr const char *kQweatherMissingCodeText = "missing";
#define QWEATHER_RESPONSE_SIZE_INVALID_FORMAT "qweather %s response size invalid"
#define QWEATHER_RESPONSE_ALLOC_FAILED_FORMAT "qweather %s response alloc failed"
} // namespace

const char *qweather_stage_text(const char *stage)
{
    return cstr_nonempty(stage) ? stage : kQweatherDefaultStage;
}

QweatherResponseBuffer::QweatherResponseBuffer(const char *stage, size_t buffer_size)
    : data_(buffer_size, HeapBufferInit::kCString),
      size_(buffer_size)
{
    if (buffer_size == 0) {
        ESP_LOGW(TAG, QWEATHER_RESPONSE_SIZE_INVALID_FORMAT, qweather_stage_text(stage));
    } else if (!data_) {
        ESP_LOGW(TAG, QWEATHER_RESPONSE_ALLOC_FAILED_FORMAT, qweather_stage_text(stage));
    }
}

QweatherResponseBuffer::~QweatherResponseBuffer() = default;

QweatherJsonRoot::QweatherJsonRoot(char *response)
    : root_(response)
{
}

QweatherJsonRoot::~QweatherJsonRoot() = default;

const char *qweather_json_string_value(const cJSON *item)
{
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

bool qweather_code_ok(const cJSON *code)
{
    const char *text = qweather_json_string_value(code);
    return text && strcmp(text, kQweatherSuccessCode) == 0;
}

const char *qweather_code_text(const cJSON *code)
{
    const char *text = qweather_json_string_value(code);
    return text ? text : kQweatherMissingCodeText;
}

const cJSON *qweather_success_item(const cJSON *root, const char *field, const cJSON **code_out)
{
    const cJSON *code = root ? cJSON_GetObjectItem(root, kQweatherJsonCodeField) : nullptr;
    if (code_out) {
        *code_out = code;
    }
    const cJSON *item = root && field ? cJSON_GetObjectItem(root, field) : nullptr;
    return qweather_code_ok(code) ? item : nullptr;
}

const cJSON *qweather_success_object(const cJSON *root, const char *field, const cJSON **code_out)
{
    const cJSON *item = qweather_success_item(root, field, code_out);
    return cJSON_IsObject(item) ? item : nullptr;
}

const cJSON *qweather_success_array(const cJSON *root, const char *field, const cJSON **code_out)
{
    const cJSON *item = qweather_success_item(root, field, code_out);
    return cJSON_IsArray(item) ? item : nullptr;
}
