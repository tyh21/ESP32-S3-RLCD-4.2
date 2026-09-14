#include <assert.h>
#include <stdio.h>

#include "solar_os_app_file_types.h"

int main(void)
{
    assert(solar_os_app_file_types_match(".gb", "/roms/TETRIS.GB"));
    assert(solar_os_app_file_types_match(".png .jpg .jpeg", "photo.JpEg"));
    assert(solar_os_app_file_types_match(".sh", "./startup.sh"));
    assert(!solar_os_app_file_types_match(".gb", "/roms/.gb"));
    assert(!solar_os_app_file_types_match(".py", "script.py.txt"));
    assert(!solar_os_app_file_types_match(NULL, "file.txt"));

    puts("app_file_types_test: ok");
    return 0;
}
