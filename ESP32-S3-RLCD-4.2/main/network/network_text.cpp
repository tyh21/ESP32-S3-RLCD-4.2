// 实现不依赖网络状态或系统服务的轻量文本处理工具。
#include "network_text.h"

#include "ascii_text.h"

void trim_ascii(char *text)
{
    trim_ascii_whitespace(text);
}
