// 声明设置页三类二次确认状态的统一访问接口。
#pragma once

enum class SettingsConfirmation {
    kFactoryReset,
    kOfflineDisable,
    kWeatherCityClear,
};

bool settings_confirmation_pending(SettingsConfirmation confirmation);
void settings_confirmation_request(SettingsConfirmation confirmation);
void settings_confirmation_clear(SettingsConfirmation confirmation);
void settings_confirmation_clear_all();
