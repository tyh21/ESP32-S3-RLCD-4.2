#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_gfx.h"

typedef struct {
  uint64_t present_us;
  uint32_t presented_frames;
  uint32_t dropped_frames;
  esp_err_t last_error;
} solar_os_gameboy_presenter_stats_t;

esp_err_t solar_os_gameboy_presenter_init(solar_os_gfx_t *gfx);
esp_err_t solar_os_gameboy_presenter_resume(void);
void solar_os_gameboy_presenter_suspend(void);
void solar_os_gameboy_presenter_deinit(void);
bool solar_os_gameboy_presenter_queue(const uint8_t *bitmap);
void solar_os_gameboy_presenter_take_stats(
    solar_os_gameboy_presenter_stats_t *stats);
