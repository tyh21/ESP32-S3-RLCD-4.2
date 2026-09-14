#pragma once

#include <stddef.h>
#include <stdint.h>

#define SOLAR_OS_GAMEBOY_ROM_HEADER_BYTES 0x150U
#define SOLAR_OS_GAMEBOY_ROM_MAX_BYTES (4U * 1024U * 1024U)

typedef enum {
  SOLAR_OS_GAMEBOY_ROM_OK = 0,
  SOLAR_OS_GAMEBOY_ROM_TOO_SMALL,
  SOLAR_OS_GAMEBOY_ROM_TOO_LARGE,
  SOLAR_OS_GAMEBOY_ROM_BAD_SIZE_CODE,
  SOLAR_OS_GAMEBOY_ROM_TRUNCATED,
  SOLAR_OS_GAMEBOY_ROM_BAD_RAM_CODE,
  SOLAR_OS_GAMEBOY_ROM_BAD_CHECKSUM,
  SOLAR_OS_GAMEBOY_ROM_CGB_ONLY,
} solar_os_gameboy_rom_status_t;

solar_os_gameboy_rom_status_t solar_os_gameboy_rom_validate(const uint8_t *rom,
                                                            size_t rom_size);
const char *
solar_os_gameboy_rom_status_name(solar_os_gameboy_rom_status_t status);
