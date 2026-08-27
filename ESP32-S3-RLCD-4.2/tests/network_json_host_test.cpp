// 验证网络 JSON 字符串复制的参数、兼容匹配和输出边界。
#include "network_json.h"

#include <assert.h>
#include <string.h>

int main()
{
    cJSON *root = cJSON_Parse(R"({"Name":"杭州","count":3,"empty":null})");
    assert(root != nullptr);

    char out[16] = "keep";
    assert(json_copy_string(root, "name", out, sizeof(out)));
    assert(strcmp(out, "杭州") == 0);

    char short_out[4] = {};
    assert(json_copy_string(root, "Name", short_out, sizeof(short_out)));
    assert(short_out[sizeof(short_out) - 1] == '\0');

    strlcpy(out, "keep", sizeof(out));
    assert(!json_copy_string(root, "missing", out, sizeof(out)));
    assert(strcmp(out, "keep") == 0);
    assert(!json_copy_string(root, "count", out, sizeof(out)));
    assert(strcmp(out, "keep") == 0);
    assert(!json_copy_string(nullptr, "Name", out, sizeof(out)));
    assert(!json_copy_string(root, nullptr, out, sizeof(out)));
    assert(!json_copy_string(root, "Name", nullptr, sizeof(out)));
    assert(!json_copy_string(root, "Name", out, 0));

    cJSON_Delete(root);
    return 0;
}
