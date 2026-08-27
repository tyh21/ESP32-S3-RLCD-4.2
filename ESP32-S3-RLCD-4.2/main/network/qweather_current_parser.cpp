// 解析 QWeather 实时天气和空气质量字段，不处理 HTTP 或缓存发布时间。
#include "qweather_current_parser.h"

#include "network_json.h"

namespace {
constexpr const char *kWeatherTextField = "text";
constexpr const char *kWeatherIconField = "icon";
constexpr const char *kWeatherTempField = "temp";
constexpr const char *kWeatherHumidityField = "humidity";
constexpr const char *kAirAqiField = "aqi";
constexpr const char *kAirCategoryField = "category";
constexpr const char *kAirPrimaryField = "primary";
constexpr const char *kAirPm25Field = "pm2p5";

bool copy_required_air_fields(const cJSON *now, WeatherAirData *air)
{
    return json_copy_string(now, kAirAqiField, air->aqi, sizeof(air->aqi)) &&
           json_copy_string(now, kAirCategoryField, air->category, sizeof(air->category));
}

void copy_optional_air_fields(const cJSON *now, WeatherAirData *air)
{
    json_copy_string(now, kAirPrimaryField, air->primary, sizeof(air->primary));
    json_copy_string(now, kAirPm25Field, air->pm2p5, sizeof(air->pm2p5));
}
} // namespace

bool parse_qweather_current_weather(const cJSON *now, WeatherData *weather)
{
    if (!cJSON_IsObject(now) || !weather) {
        return false;
    }
    return json_copy_string(now, kWeatherTextField, weather->text, sizeof(weather->text)) &&
           json_copy_string(now, kWeatherIconField, weather->icon, sizeof(weather->icon)) &&
           json_copy_string(now, kWeatherTempField, weather->temp, sizeof(weather->temp)) &&
           json_copy_string(now, kWeatherHumidityField, weather->humidity, sizeof(weather->humidity));
}

bool parse_qweather_current_air(const cJSON *now, WeatherAirData *air)
{
    if (!cJSON_IsObject(now) || !air) {
        return false;
    }
    bool ok = copy_required_air_fields(now, air);
    copy_optional_air_fields(now, air);
    return ok;
}
