// 验证 QWeather 各 endpoint URL、位置编码和容量错误分类。
#include "qweather_url.h"

#include <assert.h>
#include <string.h>

int main()
{
    assert(strcmp(qweather_api_host(), "devapi.qweather.com") == 0);
    assert(strcmp(qweather_geo_api_host(), "geoapi.qweather.com") == 0);

    char url[512] = {};
    assert(build_qweather_city_lookup_url(url, sizeof(url), "杭州") == kQweatherUrlOk);
    assert(strcmp(url,
                  "https://geoapi.qweather.com/v2/city/lookup?location=%E6%9D%AD%E5%B7%9E&number=1&range=cn&lang=zh") == 0);

    assert(build_qweather_alert_url(url, sizeof(url), "30.2875", "120.1536") == kQweatherUrlOk);
    assert(strcmp(url,
                  "https://devapi.qweather.com/weatheralert/v1/current/30.2875/120.1536?lang=zh&localTime=true") == 0);

    assert(build_qweather_now_url(url, sizeof(url), "101210101") == kQweatherUrlOk);
    assert(strcmp(url,
                  "https://devapi.qweather.com/v7/weather/now?location=101210101&lang=zh&unit=m") == 0);

    assert(build_qweather_daily_url(url, sizeof(url), "101210101", 7) == kQweatherUrlOk);
    assert(strcmp(url,
                  "https://devapi.qweather.com/v7/weather/7d?location=101210101&lang=zh&unit=m") == 0);

    assert(build_qweather_air_url(url, sizeof(url), "101210101") == kQweatherUrlOk);
    assert(strcmp(url,
                  "https://devapi.qweather.com/v7/air/now?location=101210101&lang=zh") == 0);

    assert(build_qweather_air_url(url, sizeof(url), "1") == kQweatherUrlOk);
    size_t required_url_size = strlen(url) + 1;
    char exact[128] = {};
    assert(build_qweather_air_url(exact, required_url_size, "1") == kQweatherUrlOk);
    char short_url[128] = "old";
    assert(build_qweather_air_url(short_url, required_url_size - 1, "1") ==
           kQweatherUrlTooLong);
    assert(short_url[0] == '\0');

    char long_location[64] = {};
    memset(long_location, '/', sizeof(long_location) - 1);
    strlcpy(url, "unchanged", sizeof(url));
    assert(build_qweather_city_lookup_url(url,
                                          sizeof(url),
                                          long_location) == kQweatherUrlLocationTooLong);
    assert(strcmp(url, "unchanged") == 0);

    assert(build_qweather_now_url(url, sizeof(url), nullptr) == kQweatherUrlInvalidArgument);
    assert(build_qweather_alert_url(url, sizeof(url), nullptr, "120") ==
           kQweatherUrlInvalidArgument);
    assert(build_qweather_air_url(nullptr, sizeof(url), "101210101") ==
           kQweatherUrlInvalidArgument);
    assert(build_qweather_air_url(url, 0, "101210101") == kQweatherUrlInvalidArgument);
    return 0;
}
