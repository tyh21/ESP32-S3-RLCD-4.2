// 声明配网页联网字段和手动时间字段读取接口。
#pragma once

#include "app_state.h"

inline constexpr size_t kProvisioningManualTimeFieldSize = 32;
inline constexpr size_t kProvisioningSsidFieldSize = 33;
inline constexpr size_t kProvisioningPasswordFieldSize = 65;
inline constexpr size_t kProvisioningApiKeyFieldSize = 96;
inline constexpr size_t kProvisioningWeatherCityFieldSize = kManualWeatherCityLen;

struct ProvisioningFormFields {
    char ssid[kProvisioningSsidFieldSize] = {};
    char pass[kProvisioningPasswordFieldSize] = {};
    char api_key[kProvisioningApiKeyFieldSize] = {};
    char weather_city[kProvisioningWeatherCityFieldSize] = {};
};

void read_provisioning_form_fields(const char *body, ProvisioningFormFields *fields);
void read_provisioning_manual_time(const char *body, char *out, size_t out_len);
