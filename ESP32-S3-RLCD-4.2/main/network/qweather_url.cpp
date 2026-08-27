// 集中编码并构建 QWeather 城市、预警、实时、预报和空气质量 URL。
#include "qweather_url.h"

#include "app_text_format.h"
#include "network_url.h"

#include <stdarg.h>
#include <stdio.h>

namespace {
constexpr const char *kApiHost = "devapi.qweather.com";
constexpr const char *kGeoApiHost = "geoapi.qweather.com";
constexpr const char *kCityLookupUrlFormat =
    "https://geoapi.qweather.com/v2/city/lookup?location=%s&number=1&range=cn&lang=zh";
constexpr const char *kAlertUrlFormat =
    "https://%s/weatheralert/v1/current/%s/%s?lang=zh&localTime=true";
constexpr const char *kNowUrlFormat =
    "https://%s/v7/weather/now?location=%s&lang=zh&unit=m";
constexpr const char *kDailyUrlFormat =
    "https://%s/v7/weather/%dd?location=%s&lang=zh&unit=m";
constexpr const char *kAirUrlFormat =
    "https://%s/v7/air/now?location=%s&lang=zh";

QweatherUrlStatus format_url(char *out, size_t out_len, const char *format, ...)
{
    if (!app_text::output_buffer_available(out, out_len) || !format) {
        return kQweatherUrlInvalidArgument;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(out, out_len, format, args);
    va_end(args);
    if (app_text::format_failed(written, out_len)) {
        out[0] = '\0';
        return kQweatherUrlTooLong;
    }
    return kQweatherUrlOk;
}

QweatherUrlStatus encode_location(const char *location,
                                  char *encoded_location,
                                  size_t encoded_location_len)
{
    if (!location) {
        return kQweatherUrlInvalidArgument;
    }
    return url_encode_component(location, encoded_location, encoded_location_len)
               ? kQweatherUrlOk
               : kQweatherUrlLocationTooLong;
}

QweatherUrlStatus build_city_url(char *out,
                                 size_t out_len,
                                 const char *format,
                                 const char *city_id)
{
    char encoded_location[kQweatherEncodedLocationSize] = {};
    QweatherUrlStatus status = encode_location(city_id,
                                               encoded_location,
                                               sizeof(encoded_location));
    if (status != kQweatherUrlOk) {
        return status;
    }
    return format_url(out, out_len, format, qweather_api_host(), encoded_location);
}

static_assert(kQweatherEncodedLocationSize > 1,
              "encoded QWeather location buffer must fit text and NUL");
} // namespace

const char *qweather_api_host()
{
    return kApiHost;
}

const char *qweather_geo_api_host()
{
    return kGeoApiHost;
}

QweatherUrlStatus build_qweather_city_lookup_url(char *out,
                                                 size_t out_len,
                                                 const char *location)
{
    char encoded_location[kQweatherEncodedLocationSize] = {};
    QweatherUrlStatus status = encode_location(location,
                                               encoded_location,
                                               sizeof(encoded_location));
    if (status != kQweatherUrlOk) {
        return status;
    }
    return format_url(out, out_len, kCityLookupUrlFormat, encoded_location);
}

QweatherUrlStatus build_qweather_alert_url(char *out,
                                           size_t out_len,
                                           const char *latitude,
                                           const char *longitude)
{
    if (!latitude || !longitude) {
        return kQweatherUrlInvalidArgument;
    }
    return format_url(out,
                      out_len,
                      kAlertUrlFormat,
                      qweather_api_host(),
                      latitude,
                      longitude);
}

QweatherUrlStatus build_qweather_now_url(char *out, size_t out_len, const char *city_id)
{
    return build_city_url(out, out_len, kNowUrlFormat, city_id);
}

QweatherUrlStatus build_qweather_daily_url(char *out,
                                           size_t out_len,
                                           const char *city_id,
                                           int days)
{
    char encoded_location[kQweatherEncodedLocationSize] = {};
    QweatherUrlStatus status = encode_location(city_id,
                                               encoded_location,
                                               sizeof(encoded_location));
    if (status != kQweatherUrlOk) {
        return status;
    }
    return format_url(out,
                      out_len,
                      kDailyUrlFormat,
                      qweather_api_host(),
                      days,
                      encoded_location);
}

QweatherUrlStatus build_qweather_air_url(char *out, size_t out_len, const char *city_id)
{
    return build_city_url(out, out_len, kAirUrlFormat, city_id);
}
