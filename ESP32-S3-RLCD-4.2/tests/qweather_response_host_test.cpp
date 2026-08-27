// 在主机验证 QWeather 响应缓冲、JSON 生命周期和业务成功字段边界。
#include "qweather_response.h"

#include <assert.h>
#include <string.h>

namespace {

void test_stage_and_response_buffer()
{
    assert(strcmp(qweather_stage_text(nullptr), "request") == 0);
    assert(strcmp(qweather_stage_text(""), "request") == 0);
    assert(strcmp(qweather_stage_text("daily"), "daily") == 0);

    QweatherResponseBuffer empty("test", 0);
    assert(!empty);
    assert(empty.get() == nullptr);
    assert(empty.size() == 0);

    QweatherResponseBuffer response("test", 32);
    assert(response);
    assert(response.get() != nullptr);
    assert(response.size() == 32);
    assert(response.get()[0] == '\0');
}

void test_json_root_rejects_invalid_input()
{
    QweatherJsonRoot null_root(nullptr);
    assert(!null_root);
    assert(null_root.get() == nullptr);

    char invalid[] = "{";
    QweatherJsonRoot invalid_root(invalid);
    assert(!invalid_root);
}

void test_json_root_accepts_read_only_text()
{
    NetworkJsonRoot root(R"({"code":"200"})");
    assert(root);
    assert(qweather_code_ok(cJSON_GetObjectItem(root.get(), "code")));
}

void test_success_object_and_array()
{
    char json[] = R"({"code":"200","now":{"temp":"26"},"daily":[{"fxDate":"2026-07-13"}]})";
    QweatherJsonRoot root(json);
    assert(root);

    const cJSON *code = nullptr;
    const cJSON *now = qweather_success_object(root.get(), "now", &code);
    assert(now != nullptr);
    assert(qweather_code_ok(code));
    assert(strcmp(qweather_code_text(code), "200") == 0);

    const cJSON *daily = qweather_success_array(root.get(), "daily", nullptr);
    assert(daily != nullptr);
    assert(cJSON_GetArraySize(daily) == 1);

    assert(qweather_success_array(root.get(), "now", nullptr) == nullptr);
    assert(qweather_success_object(root.get(), "daily", nullptr) == nullptr);
}

void test_failed_and_missing_business_code()
{
    char failed_json[] = R"({"code":"401","now":{"temp":"26"}})";
    QweatherJsonRoot failed_root(failed_json);
    assert(failed_root);
    const cJSON *code = nullptr;
    assert(qweather_success_item(failed_root.get(), "now", &code) == nullptr);
    assert(!qweather_code_ok(code));
    assert(strcmp(qweather_code_text(code), "401") == 0);

    char missing_json[] = R"({"now":{"temp":"26"}})";
    QweatherJsonRoot missing_root(missing_json);
    assert(missing_root);
    code = reinterpret_cast<const cJSON *>(1);
    assert(qweather_success_item(missing_root.get(), "now", &code) == nullptr);
    assert(code == nullptr);
    assert(strcmp(qweather_code_text(code), "missing") == 0);

    code = reinterpret_cast<const cJSON *>(1);
    assert(qweather_success_item(nullptr, "now", &code) == nullptr);
    assert(code == nullptr);
    assert(qweather_success_item(missing_root.get(), nullptr, nullptr) == nullptr);
    assert(qweather_json_string_value(nullptr) == nullptr);
}

} // namespace

int main()
{
    test_stage_and_response_buffer();
    test_json_root_rejects_invalid_input();
    test_json_root_accepts_read_only_text();
    test_success_object_and_array();
    test_failed_and_missing_business_code();
    return 0;
}
