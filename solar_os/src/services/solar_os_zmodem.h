#pragma once

#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "solar_os_port.h"
#include "solar_os_transfer.h"

esp_err_t solar_os_zmodem_send(const solar_os_transfer_options_t *options,
                               const solar_os_port_handle_t *port,
                               FILE *file,
                               uint64_t file_size,
                               solar_os_transfer_result_t *result);
esp_err_t solar_os_zmodem_recv(const solar_os_transfer_options_t *options,
                               const solar_os_port_handle_t *port,
                               solar_os_transfer_result_t *result);
