#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "solar_os_agent_reference.h"

#define RESULT_CAPACITY 4096U

void solar_os_memory_free(void *ptr)
{
    free(ptr);
}

static void expect_reference(const char *query,
                             const char *first,
                             const char *second)
{
    char result[RESULT_CAPACITY];
    assert(solar_os_agent_reference_search(query,
                                           result,
                                           sizeof(result)) == ESP_OK);
    assert(strlen(result) < sizeof(result));
    assert(strstr(result, "\"section\":") != NULL);
    if (strstr(result, first) == NULL || strstr(result, second) == NULL) {
        fprintf(stderr, "%s\n%s\n", query, result);
    }
    assert(strstr(result, first) != NULL);
    assert(strstr(result, second) != NULL);
}

int main(void)
{
    expect_reference("python http post response fields",
                     "post(url",
                     "status_code");
    expect_reference("lua websocket ownership limits",
                     "websocket_connect",
                     "Handles cannot");
    expect_reference("python open external imports mpy",
                     "open(path",
                     ".mpy");
    expect_reference("python json hashlib selected modules",
                     "hashlib",
                     "json");
    expect_reference("lua gpio configure pull constants",
                     "PULL_UP",
                     "configure");
    return 0;
}
