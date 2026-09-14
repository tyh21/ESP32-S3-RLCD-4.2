#include "solar_os_app_file_types.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static bool extension_equal(const char *extension,
                            size_t extension_len,
                            const char *candidate,
                            size_t candidate_len)
{
    if (extension_len != candidate_len) {
        return false;
    }
    for (size_t i = 0; i < extension_len; i++) {
        if (tolower((unsigned char)extension[i]) !=
            tolower((unsigned char)candidate[i])) {
            return false;
        }
    }
    return true;
}

bool solar_os_app_file_types_match(const char *extensions, const char *path)
{
    if (extensions == NULL || path == NULL) {
        return false;
    }

    const char *base = strrchr(path, '/');
    base = base != NULL ? base + 1 : path;
    const char *extension = strrchr(base, '.');
    if (extension == NULL || extension == base || extension[1] == '\0') {
        return false;
    }
    const size_t extension_len = strlen(extension);

    const char *candidate = extensions;
    while (*candidate != '\0') {
        while (*candidate == ' ') {
            candidate++;
        }
        const char *end = candidate;
        while (*end != '\0' && *end != ' ') {
            end++;
        }
        if (extension_equal(extension,
                            extension_len,
                            candidate,
                            (size_t)(end - candidate))) {
            return true;
        }
        candidate = end;
    }
    return false;
}
