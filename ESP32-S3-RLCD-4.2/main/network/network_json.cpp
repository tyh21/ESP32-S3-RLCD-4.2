// 实现网络响应模块共用的只读 JSON 字符串复制。
#include "network_json.h"

#include "app_text_format.h"

#include <string.h>

bool json_copy_string(const cJSON *obj,
                      const char *name,
                      char *out,
                      size_t out_len)
{
    if (!obj || !name || !app_text::output_buffer_available(out, out_len)) {
        return false;
    }
    const cJSON *item = cJSON_GetObjectItem(obj, name);
    if (!cJSON_IsString(item) || !item->valuestring) {
        return false;
    }
    strlcpy(out, item->valuestring, out_len);
    return true;
}
