#pragma once

#include "esp_err.h"
#include "u8g2.h"

#if defined(SOLAR_OS_BOARD_HAS_DISPLAY) && SOLAR_OS_BOARD_HAS_DISPLAY
#include "solar_os_board.h"
#define SOLAR_OS_VIRTUAL_DISPLAY_WIDTH ((unsigned)SOLAR_OS_BOARD_DISPLAY_WIDTH)
#define SOLAR_OS_VIRTUAL_DISPLAY_HEIGHT ((unsigned)SOLAR_OS_BOARD_DISPLAY_HEIGHT)
#else
#define SOLAR_OS_VIRTUAL_DISPLAY_WIDTH 400U
#define SOLAR_OS_VIRTUAL_DISPLAY_HEIGHT 300U
#endif

/* The virtual U8g2 target uses R1, so its native axes are exchanged. */
#define SOLAR_OS_VIRTUAL_DISPLAY_NATIVE_WIDTH SOLAR_OS_VIRTUAL_DISPLAY_HEIGHT
#define SOLAR_OS_VIRTUAL_DISPLAY_NATIVE_HEIGHT SOLAR_OS_VIRTUAL_DISPLAY_WIDTH

typedef struct solar_os_virtual_display solar_os_virtual_display_t;

esp_err_t solar_os_virtual_display_create(const char *name,
                                          solar_os_virtual_display_t **display);
esp_err_t solar_os_virtual_display_destroy(solar_os_virtual_display_t *display);
u8g2_t *solar_os_virtual_display_u8g2(solar_os_virtual_display_t *display);
