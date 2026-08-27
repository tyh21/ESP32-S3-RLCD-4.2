#include "weather_client.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WEATHER_CLIENT";

// 全局响应缓冲区（仅用于单次请求，线程安全需调用者保证）
static char *g_response_buf = NULL;
static int g_response_len = 0;
static esp_err_t g_req_err = ESP_OK;

// HTTP 事件回调 – 动态接收数据
static esp_err_t http_event_handler(esp_http_client_event_handle_t evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0) {
            char *new_buf = realloc(g_response_buf, g_response_len + evt->data_len + 1);
            if (!new_buf) {
                ESP_LOGE(TAG, "realloc failed");
                g_req_err = ESP_ERR_NO_MEM;
                return ESP_FAIL;
            }
            g_response_buf = new_buf;
            memcpy(g_response_buf + g_response_len, evt->data, evt->data_len);
            g_response_len += evt->data_len;
            g_response_buf[g_response_len] = '\0';
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP request finished");
        break;
    default:
        break;
    }
    return ESP_OK;
}

// 执行 HTTP GET 请求，返回动态分配的响应字符串（调用者 free）
static esp_err_t http_get(const char *url, char **out_buf)
{
    // 重置全局变量
    if (g_response_buf) {
        free(g_response_buf);
        g_response_buf = NULL;
    }
    g_response_len = 0;
    g_req_err = ESP_OK;

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 20000,
        .event_handler = http_event_handler,
        .skip_cert_common_name_check = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 && g_response_buf) {
            ESP_LOGI("HTTP", "Response: %.200s", g_response_buf);
            *out_buf = g_response_buf;
            g_response_buf = NULL;   // 所有权转移给调用者
            err = ESP_OK;
        } else {
            ESP_LOGE(TAG, "HTTP status %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

// 解析 OpenWeatherMap 的 5 天预报（取每天 12:00 的天气）
static char *parse_forecast(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return NULL;

    cJSON *list = cJSON_GetObjectItem(root, "list");
    if (!list || !cJSON_IsArray(list)) {
        cJSON_Delete(root);
        return NULL;
    }

    int count = cJSON_GetArraySize(list);
    char *dates[3] = {0};
    float temps[3] = {0};
    char *descs[3] = {0};
    int day_cnt = 0;
    char last_date[11] = "";

    for (int i = 0; i < count && day_cnt < 3; i++) {
        cJSON *item = cJSON_GetArrayItem(list, i);
        cJSON *dt_txt = cJSON_GetObjectItem(item, "dt_txt");
        if (!dt_txt) continue;
        char *time_str = dt_txt->valuestring;  // "2026-04-13 12:00:00"
        char date_str[11];
        strncpy(date_str, time_str, 10);
        date_str[10] = '\0';

        // 取每天中午12点的预报
        if (strcmp(date_str, last_date) != 0 && strstr(time_str, "12:00:00")) {
            strcpy(last_date, date_str);
            cJSON *main = cJSON_GetObjectItem(item, "main");
            cJSON *weather_arr = cJSON_GetObjectItem(item, "weather");
            if (main && weather_arr && cJSON_GetArraySize(weather_arr) > 0) {
                cJSON *temp = cJSON_GetObjectItem(main, "temp");
                cJSON *weather = cJSON_GetArrayItem(weather_arr, 0);
                cJSON *desc = cJSON_GetObjectItem(weather, "description");
                if (temp && desc) {
                    dates[day_cnt] = strdup(date_str);
                    temps[day_cnt] = temp->valuedouble;
                    descs[day_cnt] = strdup(desc->valuestring);
                    day_cnt++;
                }
            }
        }
    }

    cJSON_Delete(root);

    if (day_cnt == 0) {
        return strdup("无法获取天气预报");
    }

    char buffer[512];
    //snprintf(buffer, sizeof(buffer), "未来三天天气预报:\n");
    //snprintf(buffer, sizeof(buffer), "3-Day Forecast:\n");
    for (int i = 0; i < day_cnt; i++) {
        char line[128];
        //snprintf(line, sizeof(line), "%s: %.1f°C %s\n", dates[i], temps[i], descs[i]);
        snprintf(line, sizeof(line), " %s\n", descs[i]);
        //snprintf(line, sizeof(line), "%s: %.1f C %s\n", dates[i], temps[i], descs[i]);
        //snprintf(line, sizeof(line), "%s: %.1f*C %s\n", dates[i], temps[i], descs[i]);
        strcat(buffer, line);
        free(dates[i]);
        free(descs[i]);
    }
    return strdup(buffer);
}

// 对外接口：获取天气预报文本
esp_err_t weather_get_forecast(const char *city, const char *api_key, char **out_text)
{
    if (!city || !api_key || !out_text) return ESP_ERR_INVALID_ARG;

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/forecast?q=%s&appid=%s&units=metric&lang=zh_cn",
             city, api_key);

    char *response = NULL;
    esp_err_t err = http_get(url, &response);
    if (err != ESP_OK || !response) {
        ESP_LOGE(TAG, "HTTP request failed");
        if (response) free(response);
        return ESP_FAIL;
    }

    char *weather_text = parse_forecast(response);
    free(response);
    if (!weather_text) {
        ESP_LOGE(TAG, "Parse forecast failed");
        return ESP_FAIL;
    }

    *out_text = weather_text;
    return ESP_OK;
}

void weather_deinit(void)
{
    if (g_response_buf) {
        free(g_response_buf);
        g_response_buf = NULL;
    }
}

static char *parse_current_weather(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return NULL;
    cJSON *weather_arr = cJSON_GetObjectItem(root, "weather");
    cJSON *main = cJSON_GetObjectItem(root, "main");
    if (!weather_arr || !cJSON_IsArray(weather_arr) || !main) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON *first = cJSON_GetArrayItem(weather_arr, 0);
    cJSON *desc = cJSON_GetObjectItem(first, "description");
    cJSON *temp = cJSON_GetObjectItem(main, "temp");
    if (!desc || !temp) {
        cJSON_Delete(root);
        return NULL;
    }
    char *out = malloc(64);
    snprintf(out, 64, "%s %.1f°C", desc->valuestring, temp->valuedouble);
    cJSON_Delete(root);
    return out;
}

// 对外接口：获取当前天气（短文本）
esp_err_t weather_get_current(const char *city, const char *api_key, char **out_text)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=metric&lang=zh_cn",
             city, api_key);
    char *response = NULL;
    esp_err_t err = http_get(url, &response);
    if (err != ESP_OK || !response) return ESP_FAIL;
    char *text = parse_current_weather(response);
    free(response);
    if (!text) return ESP_FAIL;
    *out_text = text;
    return ESP_OK;
}
