#pragma once

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

/* ESP-IDF fixes LWIP_HOOK_IP4_ROUTE_SRC to ip4_route_src_hook. Replace that
 * hook after lwipopts.h is processed, while preserving IDF's source-address
 * selection in the SolarOS implementation. */
#undef LWIP_HOOK_IP4_ROUTE_SRC
#define LWIP_HOOK_IP4_ROUTE_SRC solar_os_lwip_ip4_route_src_hook

struct netif *solar_os_lwip_ip4_route_src_hook(const ip4_addr_t *src,
                                                const ip4_addr_t *dest);
