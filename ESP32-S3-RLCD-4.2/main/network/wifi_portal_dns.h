// 声明配网强制门户 DHCP/DNS 内部配置入口。
#pragma once

#include "esp_netif.h"

void configure_captive_portal_dhcp(esp_netif_t *ap_netif);
