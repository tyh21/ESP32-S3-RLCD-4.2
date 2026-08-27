// 验证工作页顺序 NVS 条件写入及失败时的变化标记。
#include "network_page_storage.h"

#include "app_state.h"

#include <assert.h>
#include <string.h>

namespace {
uint8_t g_saved_order[kWorkPageCount] = {};
bool g_have_saved_order = false;
esp_err_t g_set_blob_result = ESP_OK;
int g_get_blob_calls = 0;
int g_set_blob_calls = 0;

void reset_store()
{
    memset(g_saved_order, 0, sizeof(g_saved_order));
    g_have_saved_order = false;
    g_set_blob_result = ESP_OK;
    g_get_blob_calls = 0;
    g_set_blob_calls = 0;
}
} // namespace

uint8_t normalize_work_page_enabled_mask(uint8_t page_mask)
{
    return page_mask;
}

esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *)
{
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *out, size_t *len)
{
    ++g_get_blob_calls;
    if (!g_have_saved_order) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (!out || !len || *len < sizeof(g_saved_order)) {
        return ESP_FAIL;
    }
    memcpy(out, g_saved_order, sizeof(g_saved_order));
    *len = sizeof(g_saved_order);
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *value, size_t len)
{
    ++g_set_blob_calls;
    if (g_set_blob_result != ESP_OK) {
        return g_set_blob_result;
    }
    if (!value || len != sizeof(g_saved_order)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(g_saved_order, value, len);
    g_have_saved_order = true;
    return ESP_OK;
}

int main()
{
    constexpr nvs_handle_t kNvs = 1;
    const uint8_t order[kWorkPageCount] = {0, 1, 2, 3, 4, 5, 6};

    reset_store();
    bool changed = true;
    assert(network_page_storage::write_work_page_order_nvs(
               kNvs, ESP_FAIL, order, sizeof(order), &changed) == ESP_FAIL);
    assert(!changed);
    assert(g_get_blob_calls == 0);
    assert(g_set_blob_calls == 0);

    memcpy(g_saved_order, order, sizeof(order));
    g_have_saved_order = true;
    changed = true;
    assert(network_page_storage::write_work_page_order_nvs(
               kNvs, ESP_OK, order, sizeof(order), &changed) == ESP_OK);
    assert(!changed);
    assert(g_get_blob_calls == 1);
    assert(g_set_blob_calls == 0);

    uint8_t changed_order[kWorkPageCount] = {1, 0, 2, 3, 4, 5, 6};
    g_set_blob_result = ESP_FAIL;
    changed = true;
    assert(network_page_storage::write_work_page_order_nvs(
               kNvs, ESP_OK, changed_order, sizeof(changed_order), &changed) == ESP_FAIL);
    assert(!changed);
    assert(g_set_blob_calls == 1);

    g_set_blob_result = ESP_OK;
    assert(network_page_storage::write_work_page_order_nvs(
               kNvs, ESP_OK, changed_order, sizeof(changed_order), &changed) == ESP_OK);
    assert(changed);
    assert(g_set_blob_calls == 2);
    assert(memcmp(g_saved_order, changed_order, sizeof(changed_order)) == 0);
    return 0;
}
