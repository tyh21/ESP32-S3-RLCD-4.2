#pragma once

#include "driver/spi_master.h"
#include "esp_err.h"

esp_err_t solar_os_spi_bus_acquire(void);
void solar_os_spi_bus_release(void);
spi_host_device_t solar_os_spi_bus_host(void);
int solar_os_spi_bus_sclk_pin(void);
int solar_os_spi_bus_miso_pin(void);
int solar_os_spi_bus_mosi_pin(void);
size_t solar_os_spi_bus_max_transfer_size(void);
esp_err_t solar_os_spi_bus_transfer(int cs_pin,
                                    uint8_t mode,
                                    uint32_t speed_hz,
                                    const uint8_t *tx_data,
                                    uint8_t *rx_data,
                                    size_t len);
