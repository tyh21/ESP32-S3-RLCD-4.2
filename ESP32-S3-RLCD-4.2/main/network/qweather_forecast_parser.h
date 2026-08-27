// 声明 QWeather 每日预报 JSON 转换和本地天气建议接口。
#pragma once

#include "app_state.h"

const char *weather_advice_for_day(const WeatherForecastDay &today);
bool parse_qweather_forecast_days(const cJSON *daily, WeatherForecastData *forecast);
