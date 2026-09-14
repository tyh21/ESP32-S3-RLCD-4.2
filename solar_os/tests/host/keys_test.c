#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "solar_os_keys.h"

static uint8_t parse(const char *name)
{
    uint8_t key = 0;
    assert(solar_os_key_parse(name, &key));
    return key;
}

int main(void)
{
    assert(parse("UP") == SOLAR_OS_KEY_UP);
    assert(parse("up") == SOLAR_OS_KEY_UP);
    assert(parse("ARROW_UP") == SOLAR_OS_KEY_UP);
    assert(parse("ENTER") == SOLAR_OS_KEY_ENTER);
    assert(parse("ESC") == SOLAR_OS_KEY_ESCAPE);
    assert(parse("x") == 'x');
    assert(parse("X") == 'X');
    assert(strcmp(solar_os_key_name(SOLAR_OS_KEY_CTRL_SHIFT_RIGHT),
                  "CTRL_SHIFT_RIGHT") == 0);
    assert(strcmp(solar_os_key_name(SOLAR_OS_KEY_ENTER), "ENTER") == 0);

    uint8_t key = 0;
    assert(!solar_os_key_parse(NULL, &key));
    assert(!solar_os_key_parse("", &key));
    assert(!solar_os_key_parse("NOT_A_KEY", &key));
    assert(!solar_os_key_parse("UP", NULL));

    puts("keys_test: ok");
    return 0;
}
