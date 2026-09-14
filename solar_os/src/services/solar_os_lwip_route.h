#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

#define SOLAR_OS_LWIP_ROUTE_MAX 8U

typedef struct {
    ip4_addr_t network;
    ip4_addr_t netmask;
} solar_os_lwip_route_t;

/* These mutation functions must run on the lwIP TCP/IP task. */
void solar_os_lwip_route_configure(struct netif *netif,
                                   const solar_os_lwip_route_t *routes,
                                   size_t route_count,
                                   bool fail_closed);
void solar_os_lwip_route_clear(struct netif *netif);
