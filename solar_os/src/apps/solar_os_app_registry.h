#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "solar_os.h"

#define SOLAR_OS_APP_OWNER_MAX 24

typedef enum {
    SOLAR_OS_APP_CAP_TEXT = 1U << 0,
    SOLAR_OS_APP_CAP_GRAPHICS = 1U << 1,
    SOLAR_OS_APP_CAP_DISPLAY = 1U << 2,
    SOLAR_OS_APP_CAP_PORT = 1U << 3,
} solar_os_app_capability_t;

typedef struct {
    const char *name;
    const char *summary;
    const solar_os_app_t *app;
    uint32_t capabilities;
    const char *usage;
    const char *file_extensions;
    uint8_t min_argc;
    uint8_t max_argc;
} solar_os_app_registry_entry_t;

size_t solar_os_app_registry_count(void);
const solar_os_app_registry_entry_t *solar_os_app_registry_get(size_t index);
const solar_os_app_registry_entry_t *solar_os_app_registry_find(const char *name);
const solar_os_app_registry_entry_t *solar_os_app_registry_find_by_app(const solar_os_app_t *app);
const solar_os_app_registry_entry_t *solar_os_app_registry_find_opener(const char *path);
bool solar_os_app_registry_owner(const solar_os_app_t *app, char *owner, size_t owner_len);
esp_err_t solar_os_app_registry_claim(const solar_os_app_t *app,
                                      const char *owner,
                                      char *current_owner,
                                      size_t current_owner_len);
void solar_os_app_registry_release(const solar_os_app_t *app, const char *owner);
