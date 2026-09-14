#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_MQTT_URL_MAX 160
#define SOLAR_OS_MQTT_USERNAME_MAX 64
#define SOLAR_OS_MQTT_PASSWORD_MAX 96
#define SOLAR_OS_MQTT_CLIENT_ID_MAX 64
#define SOLAR_OS_MQTT_TOPIC_MAX 96
#define SOLAR_OS_MQTT_PAYLOAD_MAX 192
#define SOLAR_OS_MQTT_ERROR_MAX 64

typedef struct {
    bool initialized;
    bool configured;
    bool running;
    bool connected;
    bool username_set;
    bool password_set;
    char url[SOLAR_OS_MQTT_URL_MAX];
    char username[SOLAR_OS_MQTT_USERNAME_MAX];
    char client_id[SOLAR_OS_MQTT_CLIENT_ID_MAX];
    char last_error[SOLAR_OS_MQTT_ERROR_MAX];
    esp_err_t last_esp_error;
    int last_msg_id;
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t dropped_count;
    size_t queued_messages;
} solar_os_mqtt_status_t;

typedef struct {
    char topic[SOLAR_OS_MQTT_TOPIC_MAX];
    char payload[SOLAR_OS_MQTT_PAYLOAD_MAX];
    size_t payload_len;
    int qos;
    bool retain;
    bool truncated;
} solar_os_mqtt_message_t;

esp_err_t solar_os_mqtt_init(void);
esp_err_t solar_os_mqtt_connect(const char *url, const char *username, const char *password);
esp_err_t solar_os_mqtt_disconnect(void);
esp_err_t solar_os_mqtt_publish(const char *topic,
                                const void *payload,
                                size_t payload_len,
                                int qos,
                                bool retain,
                                int *msg_id);
esp_err_t solar_os_mqtt_subscribe(const char *topic, int qos, int *msg_id);
esp_err_t solar_os_mqtt_read_message(solar_os_mqtt_message_t *message, uint32_t timeout_ms);
esp_err_t solar_os_mqtt_get_status(solar_os_mqtt_status_t *status);
