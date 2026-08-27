// 验证 QWeather 实时天气必填字段和空气质量必填/可选字段解析语义。
#include "qweather_current_parser.h"

#include "cJSON.h"

#include <assert.h>
#include <string.h>

namespace {
cJSON *parse_json(const char *text)
{
    cJSON *root = cJSON_Parse(text);
    assert(root != nullptr);
    return root;
}
} // namespace

int main()
{
    WeatherData weather = {};
    assert(!parse_qweather_current_weather(nullptr, &weather));
    cJSON *array = parse_json("[]");
    assert(!parse_qweather_current_weather(array, &weather));
    cJSON_Delete(array);

    cJSON *now = parse_json(
        "{\"text\":\"多云\",\"icon\":\"101\",\"temp\":\"26\",\"humidity\":\"58\"}");
    assert(parse_qweather_current_weather(now, &weather));
    assert(strcmp(weather.text, "多云") == 0);
    assert(strcmp(weather.icon, "101") == 0);
    assert(strcmp(weather.temp, "26") == 0);
    assert(strcmp(weather.humidity, "58") == 0);
    cJSON_Delete(now);

    WeatherData partial_weather = {};
    now = parse_json("{\"text\":\"晴\",\"icon\":\"100\",\"temp\":\"31\"}");
    assert(!parse_qweather_current_weather(now, &partial_weather));
    assert(strcmp(partial_weather.text, "晴") == 0);
    assert(strcmp(partial_weather.icon, "100") == 0);
    assert(strcmp(partial_weather.temp, "31") == 0);
    assert(partial_weather.humidity[0] == '\0');
    cJSON_Delete(now);

    WeatherAirData air = {};
    now = parse_json(
        "{\"aqi\":\"42\",\"category\":\"优\",\"primary\":\"NA\",\"pm2p5\":\"17\"}");
    assert(parse_qweather_current_air(now, &air));
    assert(strcmp(air.aqi, "42") == 0);
    assert(strcmp(air.category, "优") == 0);
    assert(strcmp(air.primary, "NA") == 0);
    assert(strcmp(air.pm2p5, "17") == 0);
    assert(!air.ready && air.updated_at == 0);
    cJSON_Delete(now);

    WeatherAirData air_without_optional = {};
    now = parse_json("{\"aqi\":\"80\",\"category\":\"良\"}");
    assert(parse_qweather_current_air(now, &air_without_optional));
    assert(air_without_optional.primary[0] == '\0');
    assert(air_without_optional.pm2p5[0] == '\0');
    cJSON_Delete(now);

    WeatherAirData partial_air = {};
    now = parse_json("{\"aqi\":\"90\",\"primary\":\"PM2.5\",\"pm2p5\":\"35\"}");
    assert(!parse_qweather_current_air(now, &partial_air));
    assert(strcmp(partial_air.aqi, "90") == 0);
    assert(partial_air.category[0] == '\0');
    assert(strcmp(partial_air.primary, "PM2.5") == 0);
    assert(strcmp(partial_air.pm2p5, "35") == 0);
    cJSON_Delete(now);

    WeatherAirData short_circuit_air = {};
    now = parse_json("{\"category\":\"优\",\"primary\":\"NA\"}");
    assert(!parse_qweather_current_air(now, &short_circuit_air));
    assert(short_circuit_air.category[0] == '\0');
    assert(strcmp(short_circuit_air.primary, "NA") == 0);
    cJSON_Delete(now);
    return 0;
}
