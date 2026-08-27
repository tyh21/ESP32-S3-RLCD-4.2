#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief 获取未来三天的天气预报（文本格式）
 * @param city 城市拼音，如 "Beijing"
 * @param api_key OpenWeatherMap API Key
 * @param out_text 输出指针，调用者需用 free() 释放
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t weather_get_forecast(const char *city, const char *api_key, char **out_text);

/**
 * @brief 释放天气模块内部资源（如果有需要）
 */
void weather_deinit(void);

esp_err_t weather_get_current(const char *city, const char *api_key, char **out_text);   // 对外接口：获取当前天气（短文本）

#ifdef __cplusplus
}

#endif
