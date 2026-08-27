// 解析 QWeather 城市查询首项，不处理 HTTP、业务码或定位策略。
#include "qweather_city_parser.h"

#include "network_json.h"

namespace {
constexpr const char *kCityIdField = "id";
constexpr const char *kCityNameField = "name";
constexpr const char *kCityLatitudeField = "lat";
constexpr const char *kCityLongitudeField = "lon";
} // namespace

bool parse_qweather_city_location(const cJSON *location,
                                  char *city_id,
                                  size_t city_id_len,
                                  char *city_name,
                                  size_t city_name_len,
                                  char *lat_out,
                                  size_t lat_len,
                                  char *lon_out,
                                  size_t lon_len)
{
    if (!cJSON_IsObject(location)) {
        return false;
    }
    bool ok = json_copy_string(location, kCityIdField, city_id, city_id_len) &&
              json_copy_string(location, kCityNameField, city_name, city_name_len);
    if (!ok) {
        return false;
    }
    if (lat_out && lat_len > 0) {
        json_copy_string(location, kCityLatitudeField, lat_out, lat_len);
    }
    if (lon_out && lon_len > 0) {
        json_copy_string(location, kCityLongitudeField, lon_out, lon_len);
    }
    return true;
}
