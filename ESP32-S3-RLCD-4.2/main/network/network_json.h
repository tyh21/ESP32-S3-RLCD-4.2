// 声明网络响应模块共用的只读 JSON 字符串复制接口。
#pragma once

#include "cJSON.h"

#include <stddef.h>

bool json_copy_string(const cJSON *obj,
                      const char *name,
                      char *out,
                      size_t out_len);
