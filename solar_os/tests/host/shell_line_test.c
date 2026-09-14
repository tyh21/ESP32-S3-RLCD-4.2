#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "solar_os_shell_line.h"

static void test_paste_at_cursor(void)
{
    char line[16] = "ac";
    size_t length = 2;
    size_t cursor = 1;

    assert(solar_os_shell_line_paste(line,
                                     sizeof(line),
                                     &length,
                                     &cursor,
                                     "b",
                                     1) == 1);
    assert(strcmp(line, "abc") == 0);
    assert(length == 3);
    assert(cursor == 2);
}

static void test_paste_normalizes_lines_and_ignores_controls(void)
{
    char line[32] = "run ";
    size_t length = 4;
    size_t cursor = 4;
    const char clipboard[] = "one\r\ntwo\tthree\x01";

    assert(solar_os_shell_line_paste(line,
                                     sizeof(line),
                                     &length,
                                     &cursor,
                                     clipboard,
                                     sizeof(clipboard) - 1U) == 13);
    assert(strcmp(line, "run one two three") == 0);
    assert(length == strlen(line));
    assert(cursor == length);
}

static void test_paste_stops_at_line_capacity(void)
{
    char line[8] = "12";
    size_t length = 2;
    size_t cursor = 1;

    assert(solar_os_shell_line_paste(line,
                                     sizeof(line),
                                     &length,
                                     &cursor,
                                     "abcdefghi",
                                     9) == 5);
    assert(strcmp(line, "1abcde2") == 0);
    assert(length == 7);
    assert(cursor == 6);
}

int main(void)
{
    test_paste_at_cursor();
    test_paste_normalizes_lines_and_ignores_controls();
    test_paste_stops_at_line_capacity();
    return 0;
}
