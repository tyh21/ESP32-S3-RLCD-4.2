# Peanut-GB provenance

SolarOS vendors `peanut_gb.h` from:

- Project: Peanut-GB
- Upstream: https://github.com/deltabeard/Peanut-GB
- Commit: `8e656982f08663785794b84823d3e27f856fdb7f`
- Retrieved: 2026-08-04
- License: MIT; the complete notice is retained at the top of `peanut_gb.h`.

The vendored Peanut-GB header is unmodified. SolarOS provides its platform
integration in `src/apps/solar_os_gameboy.c` and compiles without the optional
twelve-colour frontend palette.

The `minigb_apu.[ch]` example from the same upstream commit is also vendored.
It is compiled for signed 16-bit audio at 16 kHz by
`src/apps/solar_os_gameboy_apu.c`; its complete MIT notice is retained in
`LICENSE.minigb_apu.txt`.
