#pragma once

#include <stdint.h>

#define FF_MAX_SS 512
#define FF_MIN_SS 512
#define FR_OK 0

typedef uint32_t DWORD;
typedef int FRESULT;

typedef struct {
    uint32_t csize;
    uint32_t n_fatent;
} FATFS;

FRESULT f_getfree(const char *path, DWORD *free_clusters, FATFS **fs);
