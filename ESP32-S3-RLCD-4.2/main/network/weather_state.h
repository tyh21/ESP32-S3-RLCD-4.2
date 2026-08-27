// 声明天气共享快照提交和 ready 事件的内部维护接口。
#pragma once

#include "app_state.h"

void clear_weather_ready_event();
void commit_weather_update_snapshot(const WeatherData &next,
                                    const WeatherAlertData &next_alert,
                                    const WeatherForecastData &next_forecast,
                                    const WeatherAirData &next_air,
                                    bool forecast_ok,
                                    bool air_ok);
