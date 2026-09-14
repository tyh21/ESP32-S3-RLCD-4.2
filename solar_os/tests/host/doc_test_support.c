#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "solar_os_log.h"
#include "solar_os_memory.h"

size_t strlcpy(char *dst, const char *src, size_t size)
{
    const size_t len = strlen(src);
    if (size > 0) {
        const size_t copy = len < size - 1U ? len : size - 1U;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}

const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "host error";
}

void *solar_os_memory_alloc(size_t size,
                            solar_os_memory_class_t memory_class,
                            const char *tag)
{
    (void)memory_class;
    (void)tag;
    return malloc(size);
}

void *solar_os_memory_calloc(size_t count,
                             size_t size,
                             solar_os_memory_class_t memory_class,
                             const char *tag)
{
    (void)memory_class;
    (void)tag;
    return calloc(count, size);
}

void *solar_os_memory_realloc(void *ptr,
                              size_t size,
                              solar_os_memory_class_t memory_class,
                              const char *tag)
{
    (void)memory_class;
    (void)tag;
    return realloc(ptr, size);
}

void solar_os_memory_free(void *ptr)
{
    free(ptr);
}

esp_err_t solar_os_log_write(solar_os_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)level;
    (void)tag;
    (void)fmt;
    return ESP_OK;
}

