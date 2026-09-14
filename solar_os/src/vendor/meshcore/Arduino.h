#pragma once

/*
 * Minimal source-compatibility surface for the audited MeshCore core. This is
 * not an Arduino runtime and intentionally exposes no hardware or scheduler.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Stream.h"

using byte = uint8_t;

static inline char *ltoa(long value, char *text, int base)
{
    if (text == nullptr || base != 10) {
        return nullptr;
    }
    (void)snprintf(text, 24, "%ld", value);
    return text;
}
