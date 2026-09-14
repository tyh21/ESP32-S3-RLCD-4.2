#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "libssh2.h"

typedef bool (*solar_os_ssh_transport_should_stop_fn)(void *user);
typedef void (*solar_os_ssh_transport_message_fn)(void *user, const char *message);

typedef struct {
    const char *host;
    uint16_t port;
    const char *username;
    const char *password;
    const char *log_tag;
    void *user;
    solar_os_ssh_transport_should_stop_fn should_stop;
    solar_os_ssh_transport_message_fn status;
    solar_os_ssh_transport_message_fn error;
    bool include_username_in_auth_status;
    bool report_password_success;
    bool report_publickey_success;
    bool allow_unverified_host_key_without_storage;
    bool include_error_code;
    bool log_resolve;
    bool log_connect_fail;
    bool log_handshake_complete;
    bool log_host_key_match;
    bool log_key_paths;
} solar_os_ssh_transport_config_t;

typedef struct {
    int socket_fd;
    LIBSSH2_SESSION *session;
    bool libssh2_ready;
} solar_os_ssh_transport_t;

esp_err_t solar_os_ssh_transport_open(const solar_os_ssh_transport_config_t *config,
                                      solar_os_ssh_transport_t *transport);
void solar_os_ssh_transport_close(solar_os_ssh_transport_t *transport,
                                  const char *disconnect_message);
int solar_os_ssh_transport_wait_socket(const solar_os_ssh_transport_config_t *config,
                                       int socket_fd,
                                       LIBSSH2_SESSION *session);
void solar_os_ssh_transport_send_libssh2_error(const solar_os_ssh_transport_config_t *config,
                                               LIBSSH2_SESSION *session,
                                               const char *prefix,
                                               int code);
