#include "wifi_time_sync.h"

#include <string.h>

#include "board_clock.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "apps/esp_sntp.h"

static const char *TAG = "wifi_time_sync";

#define WIFI_TIME_SYNC_CONNECT_TIMEOUT_MS   (15 * 1000)
#define WIFI_TIME_SYNC_NTP_TIMEOUT_MS       (10 * 1000)

static bool s_connected = false;
static bool s_synced = false;

static void wifi_time_sync_event_handler(void *arg, esp_event_base_t event_base,
                                         int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "WiFi disconnected (reason=%d)",
                 (int)((wifi_event_sta_disconnected_t *)event_data)->reason);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t wifi_time_sync_wait_connected(void)
{
    int64_t start_us = esp_timer_get_time();
    while (!s_connected) {
        if ((esp_timer_get_time() - start_us) / 1000 >= WIFI_TIME_SYNC_CONNECT_TIMEOUT_MS) {
            ESP_LOGW(TAG, "connect wifi timeout");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

static void wifi_time_sync_ntp_cb(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
}

static esp_err_t wifi_time_sync_ntp_sync(void)
{
    s_synced = false;

    board_clock_status_t clock_status = board_clock_get_status();
    int16_t tz_minutes = clock_status.synced ? clock_status.timezone_offset_minutes
                                               : (-8 * 60); /* 默认 UTC+8，JS 语义 offset=UTC-local=-480 */

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_set_time_sync_notification_cb(wifi_time_sync_ntp_cb);
    esp_sntp_init();

    int64_t start_us = esp_timer_get_time();
    while (!s_synced) {
        if ((esp_timer_get_time() - start_us) / 1000 >= WIFI_TIME_SYNC_NTP_TIMEOUT_MS) {
            ESP_LOGW(TAG, "sntp sync timeout");
            esp_sntp_stop();
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_sntp_stop();

    time_t now = time(NULL);
    struct timeval tv = {
        .tv_sec = now,
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);

    board_clock_sync_unix_ms((uint64_t)now * 1000ULL, tz_minutes);

    ESP_LOGI(TAG, "NTP time synced, tz=%d min", (int)tz_minutes);
    return ESP_OK;
}

esp_err_t wifi_time_sync_once(const char *ssid, const char *password,
                              wifi_time_sync_result_t *result)
{
    if (result != NULL) {
        result->synced = false;
        result->elapsed_ms = 0;
    }
    if (ssid == NULL || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t start_us = esp_timer_get_time();

    /* netif / event loop 通常已由 web_gamepad 初始化，这里做幂等保障 */
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "init netif failed");
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "create event loop failed");
    }

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    wifi_time_sync_event_handler, NULL),
                        TAG, "register wifi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    wifi_time_sync_event_handler, NULL),
                        TAG, "register ip event handler failed");

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "create default wifi sta failed");
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_time_sync_event_handler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_time_sync_event_handler);
        return ESP_FAIL;
    }

    /* 当前处于 AP 模式（web_gamepad），先停 AP 再切 STA */
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t sta_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    if (password != NULL) {
        strlcpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));
    }

    s_connected = false;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "set sta config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start wifi failed");

    ret = wifi_time_sync_wait_connected();
    if (ret == ESP_OK) {
        ret = wifi_time_sync_ntp_sync();
    }

    /* 断开并切回 AP 模式 */
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_err_t ap_ret = esp_wifi_start();
    if (ap_ret != ESP_OK) {
        ESP_LOGW(TAG, "restart ap failed: %s", esp_err_to_name(ap_ret));
    }

    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_time_sync_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_time_sync_event_handler);

    /* 销毁本次创建的 STA netif，避免下次调用 create 同名 netif 触发 assert */
    if (sta_netif != NULL) {
        esp_netif_destroy_default_wifi(sta_netif);
    }

    if (result != NULL) {
        result->synced = (ret == ESP_OK);
        result->elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    }

    ESP_LOGI(TAG, "time sync done: %s, elapsed=%lldms",
             ret == ESP_OK ? "ok" : esp_err_to_name(ret),
             (long long)(result != NULL ? result->elapsed_ms : 0));
    return ret;
}