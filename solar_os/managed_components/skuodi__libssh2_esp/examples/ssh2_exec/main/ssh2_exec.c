/* Copyright (C) 2025 skuodi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms,
 * with or without modification, are permitted provided
 * that the following conditions are met:
 *
 *   Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials
 *   provided with the distribution.
 *
 *   Neither the name of the copyright holder nor the names
 *   of any other contributors may be used to endorse or
 *   promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "protocol_examples_common.h"

#include <libssh2_config.h>
#include <libssh2.h>

#include "sdkconfig.h"

#define SSH_TASK_STACK_SIZE             CONFIG_EXAMPLE_SSH_TASK_STACK_SIZE
#define FS_ROOT_DIR                     CONFIG_EXAMPLE_FS_ROOT_DIR
#define SSH_HOSTNAME                    CONFIG_EXAMPLE_SSH_HOSTNAME
#define SSH_PORT                        CONFIG_EXAMPLE_SSH_PORT
#define SSH_USERNAME                    CONFIG_EXAMPLE_SSH_USERNAME

#if (CONFIG_EXAMPLE_SSH_USE_PUBKEY)
#define SSH_PASSWORD                    ""
#else
#define SSH_PASSWORD                    CONFIG_EXAMPLE_SSH_PASSWORD
#endif

#if (CONFIG_EXAMPLE_SSH_USE_PUBKEY)
#define SSH_PRIVKEY_FILE                CONFIG_EXAMPLE_SSH_PRIVKEY_FILE
#else
#define SSH_PRIVKEY_FILE                "id_rsa"
#endif

#define SSH_PUBKEY_FILE                 SSH_PRIVKEY_FILE ".pub"

#define SSH_COMMAND                     CONFIG_EXAMPLE_SSH_COMMAND

#define ENABLE_LIBSSH2_DEBUG            CONFIG_EXAMPLE_LIBSSH2_DEBUG
#define ENABLE_LIBSSH2_DEBUG_TRANS      CONFIG_EXAMPLE_LIBSSH2_DEBUG_TRANS
#define ENABLE_LIBSSH2_DEBUG_KEX        CONFIG_EXAMPLE_LIBSSH2_DEBUG_KEX
#define ENABLE_LIBSSH2_DEBUG_AUTH       CONFIG_EXAMPLE_LIBSSH2_DEBUG_AUTH
#define ENABLE_LIBSSH2_DEBUG_CONN       CONFIG_EXAMPLE_LIBSSH2_DEBUG_CONN
#define ENABLE_LIBSSH2_DEBUG_SCP        CONFIG_EXAMPLE_LIBSSH2_DEBUG_SCP
#define ENABLE_LIBSSH2_DEBUG_SFTP       CONFIG_EXAMPLE_LIBSSH2_DEBUG_SFTP
#define ENABLE_LIBSSH2_DEBUG_ERROR      CONFIG_EXAMPLE_LIBSSH2_DEBUG_ERROR
#define ENABLE_LIBSSH2_DEBUG_PUBLICKEY  CONFIG_EXAMPLE_LIBSSH2_DEBUG_PUBLICKEY
#define ENABLE_LIBSSH2_DEBUG_SOCKET     CONFIG_EXAMPLE_LIBSSH2_DEBUG_SOCKET

#define LOG_TAG                         "libssh2_example"

TaskHandle_t ssh_task_handle = NULL;

static int waitsocket(libssh2_socket_t socket_fd, LIBSSH2_SESSION *session);
int main(int argc, char *argv[]);

void dir_list(const char *path)
{
    printf("Opening path: '%s'\n", path);

    DIR *folder;
    struct dirent *entry;
    struct stat file_stat;

    char name[300];

    folder = opendir(path);
    if (!folder)
        return;

    while ((entry = readdir(folder)))
    {
        snprintf(name, sizeof(name), "%s/%s", path, entry->d_name);

        if(entry->d_type == DT_DIR)
            dir_list(name);
        else
        {
            if (stat(name, &file_stat))
                printf("|-- %s\n", name);
            else
            {
                printf("|-- %s", name);

                if(file_stat.st_size > 1000000000)
                    snprintf(name, sizeof(name),"%ld GB", file_stat.st_size / 1000000000);
                else if(file_stat.st_size > 1000000)
                    snprintf(name, sizeof(name),"%ld MB", file_stat.st_size / 1000000);
                else if(file_stat.st_size > 1000)
                    snprintf(name, sizeof(name),"%ld KB", file_stat.st_size / 1000);
                else
                    snprintf(name, sizeof(name),"%ld B", file_stat.st_size);

                printf(" | %s\n", name);
            }
        }
    }
    closedir(folder);
}

void ssh_task(void* arg)
{
    char port_str[10];
    snprintf(port_str, sizeof(port_str), "%d", SSH_PORT);
    char *argv[] =
    {   
        NULL,                               // argv[0] (executable name placeholder)
        SSH_HOSTNAME,
        SSH_USERNAME,
        SSH_PASSWORD,
        SSH_COMMAND,
        FS_ROOT_DIR "/" SSH_PUBKEY_FILE,
        FS_ROOT_DIR "/" SSH_PRIVKEY_FILE,
        port_str,
    };

    int argc = sizeof(argv)/sizeof(argv[0]);

    ESP_LOGI(LOG_TAG, "Starting SSH task...");
    int rc = main(argc, argv);

    ESP_LOGI(LOG_TAG, "SSH main completed with rc = %d", rc);
    
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(LOG_TAG, "Initializing LittleFS");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = FS_ROOT_DIR,
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    // Use settings defined above to initialize and mount LittleFS filesystem.
    // Note: esp_vfs_littlefs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
            ESP_LOGE(LOG_TAG, "Failed to mount or format filesystem");
        else if (ret == ESP_ERR_NOT_FOUND)
            ESP_LOGE(LOG_TAG, "Failed to find LittleFS partition");
        else
            ESP_LOGE(LOG_TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));

        return;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK)
        ESP_LOGI(LOG_TAG, "Partition size: total: %d, used: %d", total, used);
    else
    {
        ESP_LOGE(LOG_TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
        esp_littlefs_format(conf.partition_label);
    }

    dir_list(FS_ROOT_DIR);
    ESP_ERROR_CHECK(example_connect());

    ESP_LOGI(LOG_TAG, "Creating SSH task...");
    xTaskCreatePinnedToCore(ssh_task, "ssh_task", SSH_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &ssh_task_handle, portNUM_PROCESSORS - 1);
    
    if(ssh_task_handle)
        ESP_LOGI(LOG_TAG, "Created SSH task successfully.");
    else
        ESP_LOGE(LOG_TAG, "Failed to create SSH task!!");

}

/**
 * The following code is adapted from libssh2/example/ssh2_exec.c with minimal
 * modification to support user configuration
 */

/* Copyright (C) The libssh2 project and its contributors.
 *
 * Sample showing how to use libssh2 to execute a command remotely.
 *
 * The sample code has fixed values for host name, user name, password
 * and command to run.
 *
 * $ ./ssh2_exec 127.0.0.1 user password "uptime"
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "libssh2_setup.h"
#include <libssh2.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *hostname = "127.0.0.1";
static const char *commandline = "uptime";
static const char *pubkey = "/home/username/.ssh/id_rsa.pub";
static const char *privkey = "/home/username/.ssh/id_rsa";
static const char *username = "user";
static const char *password = "password";
static int port = 22;

static int waitsocket(libssh2_socket_t socket_fd, LIBSSH2_SESSION *session)
{
    struct timeval timeout;
    int rc;
    fd_set fd;
    fd_set *writefd = NULL;
    fd_set *readfd = NULL;
    int dir;

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    FD_ZERO(&fd);

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
    FD_SET(socket_fd, &fd);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    /* now make sure we wait in the correct direction */
    dir = libssh2_session_block_directions(session);

    if(dir & LIBSSH2_SESSION_BLOCK_INBOUND)
        readfd = &fd;

    if(dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
        writefd = &fd;

    rc = select((int)(socket_fd + 1), readfd, writefd, NULL, &timeout);

    return rc;
}

static int _resolve_domain_and_connect(const char* domain_name, uint16_t port)
{
    
  int                       rc;
  int                       sfd;
  struct addrinfo           hints;
  struct addrinfo           *result, *rp;
  char                      port_str[10];


  if (!domain_name || !strlen(domain_name))
    return LIBSSH2_INVALID_SOCKET;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;
  
  snprintf(port_str, sizeof(port_str), "%" PRIu16, port);
  
  rc = getaddrinfo(hostname, port_str, &hints, &result);
  if (rc != 0)
  {
    fprintf(stderr, "getaddrinfo error: %d\n", rc);
    return LIBSSH2_INVALID_SOCKET;
  }

  for (rp = result; rp != NULL; rp = rp->ai_next)
  {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == LIBSSH2_INVALID_SOCKET)
      continue;

    if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != LIBSSH2_INVALID_SOCKET)
      break;

    close(sfd);
  }

  freeaddrinfo(result);

  if (!rp || sfd == LIBSSH2_INVALID_SOCKET)
  {
    ESP_LOGE(LOG_TAG, "Could not resolve host: %s", domain_name);
    return LIBSSH2_INVALID_SOCKET;
  }
  return sfd;
}

int main(int argc, char *argv[])
{
    libssh2_socket_t sock;
    const char *fingerprint;
    int rc;
    LIBSSH2_SESSION *session = NULL;
    LIBSSH2_CHANNEL *channel;
    int exitcode;
    char *exitsignal = NULL;
    ssize_t bytecount = 0;
    size_t len;
    LIBSSH2_KNOWNHOSTS *nh;
    int type;

#ifdef _WIN32
    WSADATA wsadata;

    rc = WSAStartup(MAKEWORD(2, 0), &wsadata);
    if(rc) {
        fprintf(stderr, "WSAStartup failed with error: %d\n", rc);
        return 1;
    }
#endif

    if(argc > 1) {
        hostname = argv[1];  /* must be ip address only */
    }
    if(argc > 2) {
        username = argv[2];
    }
    if(argc > 3) {
        password = argv[3];
    }
    if(argc > 4) {
        commandline = argv[4];
    }
    if(argc > 5) {
        pubkey = argv[5];
    }
    if(argc > 6) {
        privkey = argv[6];
    }
    if(argc > 7) {
        port = atoi(argv[7]);
    }

    rc = libssh2_init(0);
    if(rc) {
        fprintf(stderr, "libssh2 initialization failed (%d)\n", rc);
        return 1;
    }

    sock = _resolve_domain_and_connect(hostname, port);

    if(sock == LIBSSH2_INVALID_SOCKET)
        return 1;

    /* Create a session instance */
    session = libssh2_session_init();
    if(!session) {
        fprintf(stderr, "Could not initialize SSH session.\n");
        goto shutdown;
    }

#if (ENABLE_LIBSSH2_DEBUG)
    libssh2_trace(session, 0
#if (ENABLE_LIBSSH2_DEBUG_TRANS)
    | LIBSSH2_TRACE_TRANS       // transaction
#endif
#if (ENABLE_LIBSSH2_DEBUG_KEX)
    | LIBSSH2_TRACE_KEX         // key exchange
#endif
#if (ENABLE_LIBSSH2_DEBUG_AUTH)
    | LIBSSH2_TRACE_AUTH        // server authentication
#endif
#if (ENABLE_LIBSSH2_DEBUG_CONN)
    | LIBSSH2_TRACE_CONN        // connection
#endif
#if (ENABLE_LIBSSH2_DEBUG_SCP)
    | LIBSSH2_TRACE_SCP         // SCP
#endif
#if (ENABLE_LIBSSH2_DEBUG_SFTP)
    | LIBSSH2_TRACE_SFTP        // SFTP
#endif
#if (ENABLE_LIBSSH2_DEBUG_ERROR)
    | LIBSSH2_TRACE_ERROR       // Errors
#endif
#if (ENABLE_LIBSSH2_DEBUG_PUBLICKEY)
    | LIBSSH2_TRACE_PUBLICKEY   // Public key user authentication
#endif
#if (ENABLE_LIBSSH2_DEBUG_SOCKET)
    | LIBSSH2_TRACE_SOCKET      // sockets
#endif
    );
#endif

    /* tell libssh2 we want it all done non-blocking */
    libssh2_session_set_blocking(session, 0);

    /* ... start it up. This will trade welcome banners, exchange keys,
     * and setup crypto, compression, and MAC layers
     */
    while((rc = libssh2_session_handshake(session, sock)) ==
          LIBSSH2_ERROR_EAGAIN);
    if(rc) {
        fprintf(stderr, "Failure establishing SSH session: %d\n", rc);
        goto shutdown;
    }

    nh = libssh2_knownhost_init(session);
    if(!nh) {
        /* eeek, do cleanup here */
        return 2;
    }

    /* read all hosts from here */
    libssh2_knownhost_readfile(nh, FS_ROOT_DIR "/" "known_hosts",
                               LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    /* store all known hosts to here */
    libssh2_knownhost_writefile(nh, FS_ROOT_DIR "/" "dumpfile",
                                LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    fingerprint = libssh2_session_hostkey(session, &len, &type);
    if(fingerprint) {
        struct libssh2_knownhost *host;
        int check = libssh2_knownhost_checkp(nh, hostname, port,
                                             fingerprint, len,
                                             LIBSSH2_KNOWNHOST_TYPE_PLAIN|
                                             LIBSSH2_KNOWNHOST_KEYENC_RAW,
                                             &host);

        fprintf(stderr, "Host check: %d, key: %s\n", check,
                (check <= LIBSSH2_KNOWNHOST_CHECK_MISMATCH) ?
                host->key : "<none>");

        /*****
         * At this point, we could verify that 'check' tells us the key is
         * fine or bail out.
         *****/
    }
    else {
        /* eeek, do cleanup here */
        return 3;
    }
    libssh2_knownhost_free(nh);

    if(strlen(password) != 0) {
        /* We could authenticate via password */
        while((rc = libssh2_userauth_password(session, username, password)) ==
              LIBSSH2_ERROR_EAGAIN);
        if(rc) {
            fprintf(stderr, "Authentication by password failed.\n");
            goto shutdown;
        }
    }
    else {
        /* Or by public key */
        while((rc = libssh2_userauth_publickey_fromfile(session, username,
                                                        pubkey, privkey,
                                                        NULL)) ==
              LIBSSH2_ERROR_EAGAIN);

        if(rc) {
            fprintf(stderr, "Authentication by public key failed.\n");
            goto shutdown;
        }
    }

#if 0
    libssh2_trace(session, ~0);
#endif

    /* Exec non-blocking on the remote host */
    do {
        channel = libssh2_channel_open_session(session);
        if(channel ||
           libssh2_session_last_error(session, NULL, NULL, 0) !=
           LIBSSH2_ERROR_EAGAIN)
            break;
        waitsocket(sock, session);
    } while(1);
    if(!channel) {
        fprintf(stderr, "Error\n");
        exit(1);
    }
    while((rc = libssh2_channel_exec(channel, commandline)) ==
          LIBSSH2_ERROR_EAGAIN) {
        waitsocket(sock, session);
    }
    if(rc) {
        fprintf(stderr, "exec error\n");
        exit(1);
    }
    for(;;) {
        ssize_t nread;
        /* loop until we block */
        do {
            char buffer[0x4000];
            nread = libssh2_channel_read(channel, buffer, sizeof(buffer));
            if(nread > 0) {
                ssize_t i;
                bytecount += nread;
                fprintf(stderr, "We read:\n");
                for(i = 0; i < nread; ++i)
                    fputc(buffer[i], stderr);
                fprintf(stderr, "\n");
            }
            else {
                if(nread != LIBSSH2_ERROR_EAGAIN)
                    /* no need to output this for the EAGAIN case */
                    fprintf(stderr, "libssh2_channel_read returned %ld\n",
                            (long)nread);
            }
        } while(nread > 0);

        /* this is due to blocking that would occur otherwise so we loop on
           this condition */
        if(nread == LIBSSH2_ERROR_EAGAIN) {
            waitsocket(sock, session);
        }
        else
            break;
    }
    exitcode = 127;
    while((rc = libssh2_channel_close(channel)) == LIBSSH2_ERROR_EAGAIN)
        waitsocket(sock, session);

    if(rc == 0) {
        exitcode = libssh2_channel_get_exit_status(channel);
        libssh2_channel_get_exit_signal(channel, &exitsignal,
                                        NULL, NULL, NULL, NULL, NULL);
    }

    if(exitsignal)
        fprintf(stderr, "\nGot signal: %s\n",
                exitsignal ? exitsignal : "none");
    else
        fprintf(stderr, "\nEXIT: %d bytecount: %ld\n",
                exitcode, (long)bytecount);

    libssh2_channel_free(channel);
    channel = NULL;

shutdown:

    if(session) {
        libssh2_session_disconnect(session, "Normal Shutdown");
        libssh2_session_free(session);
    }

    if(sock != LIBSSH2_INVALID_SOCKET) {
        shutdown(sock, 2);
        LIBSSH2_SOCKET_CLOSE(sock);
    }

    fprintf(stderr, "all done\n");

    libssh2_exit();

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
