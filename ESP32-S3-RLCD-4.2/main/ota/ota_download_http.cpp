// 管理 OTA 固件下载 HTTP client、手动重定向和所有失败出口清理。
#include "ota_download_http.h"

#include "app_state.h"
#include "ota_validation.h"

#include <esp_crt_bundle.h>
#include <esp_log.h>

#include <cstring>
#include <strings.h>

namespace {

constexpr int kMaxRedirects = 5;
constexpr int kHttpTxBufferSize = 2048;
constexpr const char *kHttpClientInitFailedLog = "OTA http client init failed";
constexpr const char *kRedirectLimitReachedLog = "OTA redirect limit reached";
#define OTA_HTTP_HEADER_FAILED_FORMAT "OTA http header failed: %s"
#define OTA_HTTP_OPEN_FAILED_FORMAT "OTA http open failed: %s"
#define OTA_REDIRECT_STATUS_FORMAT "OTA redirect status=%d location=%s"
#define OTA_REDIRECT_LOCATION_INVALID_FORMAT "OTA redirect location invalid len=%u"
#define OTA_HTTP_STATUS_FAILED_FORMAT "OTA http status=%d content_len=%d"

} // namespace

OtaDownloadHttpSession::~OtaDownloadHttpSession()
{
    close();
}

esp_err_t OtaDownloadHttpSession::event_handler(esp_http_client_event_t *event)
{
    if (!event || event->event_id != HTTP_EVENT_ON_HEADER ||
        !event->user_data || !event->header_key || !event->header_value) {
        return ESP_OK;
    }
    OtaDownloadHttpSession *session =
        static_cast<OtaDownloadHttpSession *>(event->user_data);
    if (strcasecmp(event->header_key, "Location") == 0) {
        strlcpy(session->redirect_url_,
                event->header_value,
                sizeof(session->redirect_url_));
    }
    return ESP_OK;
}

bool OtaDownloadHttpSession::open(const char *url)
{
    close();
    if (!url || url[0] == '\0') {
        return false;
    }
    char current_url[kRedirectUrlCapacity] = {};
    strlcpy(current_url, url, sizeof(current_url));

    for (int redirect = 0; redirect <= kMaxRedirects; ++redirect) {
        redirect_url_[0] = '\0';
        esp_http_client_config_t config = {};
        config.url = current_url;
        config.timeout_ms = kOtaHttpTimeoutMs;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.disable_auto_redirect = true;
        config.max_redirection_count = kMaxRedirects;
        config.keep_alive_enable = true;
        config.buffer_size = kOtaDownloadBufferSize;
        config.buffer_size_tx = kHttpTxBufferSize;
        config.event_handler = event_handler;
        config.user_data = this;

        client_ = esp_http_client_init(&config);
        if (!client_) {
            ESP_LOGW(TAG, "%s", kHttpClientInitFailedLog);
            return false;
        }
        esp_err_t err = esp_http_client_set_header(
            client_,
            "Accept",
            "application/octet-stream,*/*");
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     OTA_HTTP_HEADER_FAILED_FORMAT,
                     esp_err_to_name(err));
            close();
            return false;
        }
        err = esp_http_client_open(client_, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, OTA_HTTP_OPEN_FAILED_FORMAT, esp_err_to_name(err));
            close();
            return false;
        }
        opened_ = true;
        content_length_ = esp_http_client_fetch_headers(client_);
        int status = esp_http_client_get_status_code(client_);
        if (ota_is_http_redirect_status(status)) {
            ESP_LOGI(TAG,
                     OTA_REDIRECT_STATUS_FORMAT,
                     status,
                     redirect_url_[0] ? redirect_url_ : "--");
            esp_http_client_close(client_);
            esp_http_client_cleanup(client_);
            client_ = nullptr;
            opened_ = false;
            content_length_ = 0;
            size_t redirect_len = strlen(redirect_url_);
            if (redirect_url_[0] == '\0' ||
                redirect_len >= sizeof(current_url)) {
                ESP_LOGW(TAG,
                         OTA_REDIRECT_LOCATION_INVALID_FORMAT,
                         static_cast<unsigned>(redirect_len));
                return false;
            }
            strlcpy(current_url, redirect_url_, sizeof(current_url));
            continue;
        }
        if (!ota_is_http_success_status(status)) {
            ESP_LOGW(TAG,
                     OTA_HTTP_STATUS_FAILED_FORMAT,
                     status,
                     content_length_);
            close();
            return false;
        }
        return true;
    }

    ESP_LOGW(TAG, "%s", kRedirectLimitReachedLog);
    return false;
}

void OtaDownloadHttpSession::close()
{
    if (client_) {
        if (opened_) {
            esp_http_client_close(client_);
        }
        esp_http_client_cleanup(client_);
        client_ = nullptr;
    }
    opened_ = false;
    content_length_ = 0;
    redirect_url_[0] = '\0';
}
