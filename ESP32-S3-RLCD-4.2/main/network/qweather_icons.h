// 声明 QWeather 图标代码到字体码点和 UTF-8 文本的映射接口。
#pragma once

#include <stdint.h>

uint32_t weather_icon_codepoint(const char *code);
const char *weather_icon_text(const char *code);
