// 维护天气数据的一致快照、成功更新时间和 ready 事件发布。
#include "weather_state.h"

#include "network_services.h"

namespace {
portMUX_TYPE s_weather_state_mux = portMUX_INITIALIZER_UNLOCKED;
WeatherData s_weather;
WeatherAlertData s_weather_alert;
WeatherForecastData s_weather_forecast;
WeatherAirData s_weather_air;
time_t s_last_weather_sync_time = 0;
#define WEATHER_UPDATED_LOG_FORMAT "weather updated: %s %s %sC %s%% icon=%s forecast=%s air=%s"
constexpr const char *kWeatherFetchStatusOk = "ok";
constexpr const char *kWeatherFetchStatusCached = "cached";
constexpr const char *kWeatherReadyEventUnavailableLog =
    "weather ready event skipped: app events unavailable";

void publish_weather_ready_event()
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", kWeatherReadyEventUnavailableLog);
        return;
    }
    xEventGroupSetBits(g_app_events, kWeatherReadyBit);
}
} // namespace

void get_weather_full_snapshot(WeatherData *weather,
                               WeatherAlertData *alert,
                               WeatherForecastData *forecast,
                               WeatherAirData *air)
{
    portENTER_CRITICAL(&s_weather_state_mux);
    if (weather) {
        *weather = s_weather;
    }
    if (alert) {
        *alert = s_weather_alert;
    }
    if (forecast) {
        *forecast = s_weather_forecast;
    }
    if (air) {
        *air = s_weather_air;
    }
    portEXIT_CRITICAL(&s_weather_state_mux);
}

void get_weather_snapshot(WeatherData *weather, WeatherAlertData *alert)
{
    get_weather_full_snapshot(weather, alert, nullptr, nullptr);
}

void get_weather_forecast_snapshot(WeatherForecastData *forecast)
{
    if (!forecast) {
        return;
    }
    get_weather_full_snapshot(nullptr, nullptr, forecast, nullptr);
}

void get_weather_air_snapshot(WeatherAirData *air)
{
    if (!air) {
        return;
    }
    get_weather_full_snapshot(nullptr, nullptr, nullptr, air);
}

time_t get_last_weather_sync_time()
{
    portENTER_CRITICAL(&s_weather_state_mux);
    const time_t last_sync_time = s_last_weather_sync_time;
    portEXIT_CRITICAL(&s_weather_state_mux);
    return last_sync_time;
}

bool weather_extended_data_ready()
{
    WeatherForecastData forecast = {};
    WeatherAirData air = {};
    get_weather_full_snapshot(nullptr, nullptr, &forecast, &air);
    return forecast.ready &&
           forecast.count > 0 &&
           forecast.days[0].valid &&
           air.ready;
}

void clear_weather_ready_event()
{
    if (!g_app_events) {
        ESP_LOGW(TAG, "%s", kWeatherReadyEventUnavailableLog);
        return;
    }
    xEventGroupClearBits(g_app_events, kWeatherReadyBit);
}

void commit_weather_update_snapshot(const WeatherData &next,
                                    const WeatherAlertData &next_alert,
                                    const WeatherForecastData &next_forecast,
                                    const WeatherAirData &next_air,
                                    bool forecast_ok,
                                    bool air_ok)
{
    time_t now = 0;
    time(&now);
    portENTER_CRITICAL(&s_weather_state_mux);
    s_weather = next;
    s_weather_alert = next_alert;
    if (forecast_ok) {
        s_weather_forecast = next_forecast;
    }
    if (air_ok) {
        s_weather_air = next_air;
    }
    s_last_weather_sync_time = now;
    portEXIT_CRITICAL(&s_weather_state_mux);
    publish_weather_ready_event();
    ESP_LOGI(TAG, WEATHER_UPDATED_LOG_FORMAT,
             next.city,
             next.text,
             next.temp,
             next.humidity,
             next.icon,
             forecast_ok ? kWeatherFetchStatusOk : kWeatherFetchStatusCached,
             air_ok ? kWeatherFetchStatusOk : kWeatherFetchStatusCached);
}
