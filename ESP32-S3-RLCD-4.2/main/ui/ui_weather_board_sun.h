// 提供天气看板日出、日落目标选择和分钟倒计时纯计算。
#pragma once

#include "app_state.h"

inline constexpr const char *kWeatherBoardSunCountdownPlaceholder = "距日落 --:--";

const WeatherForecastDay *weather_board_forecast_day_or_null(const WeatherForecastData &forecast,
                                                              int index);
void format_weather_board_sun_countdown(const struct tm &local,
                                        const WeatherForecastData &forecast,
                                        char *out,
                                        size_t out_len);
