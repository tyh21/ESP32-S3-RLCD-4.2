#include <stdio.h>
#include "esp_wifi_bsp.h"
#include "esp_wifi.h"  
#include "esp_event.h" 
#include "nvs_flash.h" 
#include "esp_log.h"

#include "esp_sntp.h"
#include "time.h"
#include "lwip/err.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "string.h" 

EventGroupHandle_t wifi_even_ = NULL;

esp_bsp_t user_esp_bsp;

static esp_netif_t *net = NULL;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void example_scan_wifi_task(void *arg);
void espwifi_init(void)
{
    memset(&user_esp_bsp,0,sizeof(esp_bsp_t));
    wifi_even_ = xEventGroupCreate();
    nvs_flash_init();                           // Initialize default NVS storage
    esp_netif_init();                           // Initialize TCP/IP stack
    esp_event_loop_create_default();            // Create default event loop
    net = esp_netif_create_default_wifi_sta();  // Add TCP/IP stack to the default event loop
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Default configuration
    esp_wifi_init(&cfg);                                 // Initialize WiFi
    esp_event_handler_instance_t Instance_WIFI_IP;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &Instance_WIFI_IP);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &Instance_WIFI_IP);
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "321",
            .password = "3xe567z5",
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);               // Set mode to STA
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config); // Configure WiFi
    esp_wifi_start();                               // Start WiFi
    esp_wifi_connect();
    xTaskCreatePinnedToCore(example_scan_wifi_task, "example_scan_wifi_task", 3000, NULL, 2, NULL,0);   
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI("wifiSta", "Event: %s, id=%d", event_base, event_id);
    
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI("wifiSta", "WiFi STA started, connecting to AP...");
        xEventGroupSetBits(wifi_even_, 0x01);
    }
    else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI("wifiSta", "Connected to AP! Waiting for IP...");
    }
    else if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip[25];
        uint32_t pxip = event->ip_info.ip.addr;
        sprintf(ip, "%d.%d.%d.%d", (uint8_t)(pxip), (uint8_t)(pxip >> 8), (uint8_t)(pxip >> 16), (uint8_t)(pxip >> 24));
        ESP_LOGI("wifiSta", "Got IP: %s", ip);
        xEventGroupSetBits(wifi_even_, 0x04);
    }
    else if(event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW("wifiSta", "Disconnected, reason: %d", disconnected->reason);
        xEventGroupClearBits(wifi_even_, 0x04);
    }
}

void espwifi_deinit(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(net);
    esp_event_loop_delete_default();
    //esp_netif_deinit();
    //nvs_flash_deinit();
}



static void example_scan_wifi_task(void *arg)
{
    uint16_t rec = 0;
    
    // 等待 WiFi 启动完成
    EventBits_t even = xEventGroupWaitBits(wifi_even_, 0x01, pdTRUE, pdTRUE, pdMS_TO_TICKS(15000));
    
    if (even & 0x01) {
        // 等待连接成功或超时
        EventBits_t conn_bits = xEventGroupWaitBits(wifi_even_, 0x04, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
        
        if (conn_bits & 0x04) {
            ESP_LOGI("wifiScan", "WiFi connected, starting scan...");
            // 连接成功后再扫描
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_start(NULL, true));
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_num(&rec));
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_clear_ap_list());
        } else {
            ESP_LOGW("wifiScan", "WiFi connection timeout, scan anyway...");
            // 超时后仍然扫描（可选）
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_start(NULL, true));
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_num(&rec));
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_clear_ap_list());
        }
    }
    
    user_esp_bsp.apNum = rec;
    xEventGroupSetBits(wifi_even_, 0x02);  // 通知扫描完成
    vTaskDelete(NULL);
}

// 网络时间获取函数
bool sync_time_from_network(int timeout_ms) {
    // 1. 测试 DNS 解析
    ESP_LOGI("TimeSync", "Testing DNS: ntp.aliyun.com");
    struct hostent *host = gethostbyname("ntp.aliyun.com");
    if (host != NULL) {
        ESP_LOGI("TimeSync", "DNS OK: %s", inet_ntoa(*(struct in_addr*)host->h_addr));
    } else {
        ESP_LOGE("TimeSync", "DNS FAILED!");
        return false;
    }
    
    // 2. 设置 NTP 服务器
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "203.107.6.88");
    
    // 3. 初始化 SNTP
    esp_sntp_init();
    setenv("TZ", "CST-8", 1);     // 中国标准时间，UTC+8
    tzset();                       // 生效时区设置
    
    // 4. 等待时间同步（通过检查系统时间）
    int retry = 0;
    int max_retry = timeout_ms / 1000;
    bool time_updated = false;
    
    ESP_LOGI("TimeSync", "SNTP started, waiting up to %d seconds...", max_retry);
    
    while (retry < max_retry && !time_updated) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
        
        // 检查系统时间
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // 如果年份大于 2023，说明时间已更新
        if (timeinfo.tm_year + 1900 >= 2024) {
            ESP_LOGI("TimeSync", "Time updated at retry %d/%d", retry, max_retry);
            time_updated = true;
            break;
        }
        
        // 打印状态用于调试
        sntp_sync_status_t status = esp_sntp_get_sync_status();
        ESP_LOGI("TimeSync", "Retry %d/%d, sync status: %d, time: %04d", 
                 retry, max_retry, status, timeinfo.tm_year + 1900);
    }
    
    // 5. 获取最终时间
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (timeinfo.tm_year + 1900 >= 2024) {
        ESP_LOGI("TimeSync", "✅ Time synchronized: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    } else {
        ESP_LOGW("TimeSync", "❌ Time sync failed, time: %04d", timeinfo.tm_year + 1900);
        return false;
    }
}
