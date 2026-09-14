#include "solar_os_gameboy_rom.h"

#define GAMEBOY_HEADER_CGB_FLAG 0x143U
#define GAMEBOY_HEADER_ROM_SIZE 0x148U
#define GAMEBOY_HEADER_RAM_SIZE 0x149U
#define GAMEBOY_HEADER_CHECKSUM_START 0x134U
#define GAMEBOY_HEADER_CHECKSUM_END 0x14CU
#define GAMEBOY_HEADER_CHECKSUM 0x14DU

solar_os_gameboy_rom_status_t solar_os_gameboy_rom_validate(const uint8_t *rom,
                                                            size_t rom_size) {
  if (rom == NULL || rom_size < SOLAR_OS_GAMEBOY_ROM_HEADER_BYTES) {
    return SOLAR_OS_GAMEBOY_ROM_TOO_SMALL;
  }
  if (rom_size > SOLAR_OS_GAMEBOY_ROM_MAX_BYTES) {
    return SOLAR_OS_GAMEBOY_ROM_TOO_LARGE;
  }

  const uint8_t rom_size_code = rom[GAMEBOY_HEADER_ROM_SIZE];
  if (rom_size_code > 7U) {
    return SOLAR_OS_GAMEBOY_ROM_BAD_SIZE_CODE;
  }
  const size_t expected_size = (size_t)(32U * 1024U) << rom_size_code;
  if (rom_size < expected_size) {
    return SOLAR_OS_GAMEBOY_ROM_TRUNCATED;
  }
  if (rom[GAMEBOY_HEADER_RAM_SIZE] > 5U) {
    return SOLAR_OS_GAMEBOY_ROM_BAD_RAM_CODE;
  }

  uint8_t checksum = 0;
  for (size_t i = GAMEBOY_HEADER_CHECKSUM_START;
       i <= GAMEBOY_HEADER_CHECKSUM_END; i++) {
    checksum = (uint8_t)(checksum - rom[i] - 1U);
  }
  if (checksum != rom[GAMEBOY_HEADER_CHECKSUM]) {
    return SOLAR_OS_GAMEBOY_ROM_BAD_CHECKSUM;
  }
  if (rom[GAMEBOY_HEADER_CGB_FLAG] == 0xC0U) {
    return SOLAR_OS_GAMEBOY_ROM_CGB_ONLY;
  }
  return SOLAR_OS_GAMEBOY_ROM_OK;
}

const char *
solar_os_gameboy_rom_status_name(solar_os_gameboy_rom_status_t status) {
  switch (status) {
  case SOLAR_OS_GAMEBOY_ROM_OK:
    return "ok";
  case SOLAR_OS_GAMEBOY_ROM_TOO_SMALL:
    return "file is too small to contain a Game Boy header";
  case SOLAR_OS_GAMEBOY_ROM_TOO_LARGE:
    return "ROM exceeds the 4 MiB experimental limit";
  case SOLAR_OS_GAMEBOY_ROM_BAD_SIZE_CODE:
    return "unsupported ROM size code";
  case SOLAR_OS_GAMEBOY_ROM_TRUNCATED:
    return "ROM is shorter than its cartridge header declares";
  case SOLAR_OS_GAMEBOY_ROM_BAD_RAM_CODE:
    return "invalid cartridge RAM size code";
  case SOLAR_OS_GAMEBOY_ROM_BAD_CHECKSUM:
    return "invalid cartridge header checksum";
  case SOLAR_OS_GAMEBOY_ROM_CGB_ONLY:
    return "Game Boy Color-only ROMs are not supported";
  default:
    return "invalid ROM";
  }
}
