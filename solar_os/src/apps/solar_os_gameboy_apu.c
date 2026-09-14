/* Compile the vendored MiniGB APU only when the selected flavor has synth. */
#ifdef SOLAR_OS_GAMEBOY_APU_HOST_TEST
#define SOLAR_OS_PACKAGE_SERVICE_SYNTH 1
#else
#include "solar_os_config.h"
#endif

#if SOLAR_OS_PACKAGE_SERVICE_SYNTH
#define AUDIO_SAMPLE_RATE 16000
#define MINIGB_APU_AUDIO_FORMAT_S16SYS 1
#include "vendor/peanut_gb/minigb_apu.c"
#endif
