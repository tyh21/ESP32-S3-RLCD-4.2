// 声明设置页二级菜单动态文案生成接口。
#pragma once

#include <stddef.h>

inline constexpr size_t kSettingsSecondaryTextSize = 56;

bool settings_secondary_index_valid(int index);
void set_secondary_text(char items[][kSettingsSecondaryTextSize],
                        int index,
                        const char *text);
void format_secondary_text(char items[][kSettingsSecondaryTextSize],
                           int index,
                           const char *format,
                           ...);
void populate_settings_secondary_items(
    int primary,
    char secondary_items[][kSettingsSecondaryTextSize]);
