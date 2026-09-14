#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#define SOLAR_OS_HTTP_ROUTE_OWNER_MAX 24
#define SOLAR_OS_HTTP_ROUTE_URI_MAX 96
#define SOLAR_OS_HTTP_BEARER_TOKEN_MAX 7

typedef enum {
    SOLAR_OS_HTTP_AUTH_PUBLIC,
    SOLAR_OS_HTTP_AUTH_VIEW,
    SOLAR_OS_HTTP_AUTH_CONTROL,
} solar_os_http_auth_t;

typedef esp_err_t (*solar_os_http_route_handler_t)(httpd_req_t *req, void *user);

typedef struct {
    const char *owner;
    const char *uri;
    httpd_method_t method;
    bool prefix;
    solar_os_http_auth_t auth;
    solar_os_http_route_handler_t handler;
    void *user;
} solar_os_http_route_t;

esp_err_t solar_os_http_server_register_route(const solar_os_http_route_t *route);
esp_err_t solar_os_http_server_unregister_owner(const char *owner);
bool solar_os_http_server_get_bearer_token(char *token, size_t token_len);
uint16_t solar_os_http_server_port(void);
