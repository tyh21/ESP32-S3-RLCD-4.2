#include <assert.h>
#include <stdio.h>

#include "solar_os_terminal_geometry.h"

static void expect_geometry(int height, int status, int footer, int line, int ascent)
{
    solar_os_terminal_geometry_t geometry;
    assert(solar_os_terminal_geometry_compute(height, status, footer, line, ascent,
                                              64, &geometry));
    assert(geometry.rows >= 1);
    assert(geometry.grid_top == status);
    assert(geometry.baseline_offset == geometry.grid_top + ascent);
    assert(geometry.grid_top + (int)geometry.rows * line <= geometry.content_bottom);
    const int bottom_gap = geometry.content_bottom -
                           (geometry.grid_top + (int)geometry.rows * line);
    assert(bottom_gap >= 0);
    assert(bottom_gap < line);
}
int main(void)
{
    const int heights[] = {200, 240, 288, 300};
    const int lines[] = {10, 12, 14, 16, 18, 20};
    for (size_t h = 0; h < sizeof(heights) / sizeof(heights[0]); h++) {
        for (size_t l = 0; l < sizeof(lines) / sizeof(lines[0]); l++) {
            expect_geometry(heights[h], 16, 0, lines[l], lines[l] - 2);
            expect_geometry(heights[h], 16, lines[l], lines[l], lines[l] - 2);
            expect_geometry(heights[h], 0, 0, lines[l], lines[l] - 2);
        }
    }
    puts("terminal_geometry_test: ok");
    return 0;
}
