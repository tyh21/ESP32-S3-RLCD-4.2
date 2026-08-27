// 验证图片时钟内置图库按星期、自定义图库按连续日期轮换及回退索引。
#include "ui_gallery_selection.h"

#include <assert.h>

int main()
{
    GalleryImageSelection selection = {};

    assert(gallery_image_selection_for_date(2026, 7, 12, 0, 0, 7, &selection));
    assert(selection.image_index == 0);
    assert(selection.builtin_index == 0);
    assert(!selection.uses_custom_gallery);

    assert(gallery_image_selection_for_date(2026, 7, 13, 1, 0, 7, &selection));
    assert(selection.image_index == 1);
    assert(selection.builtin_index == 1);
    assert(!selection.uses_custom_gallery);

    assert(gallery_image_selection_for_date(2026, 7, 12, 0, 24, 7, &selection));
    int sunday_custom_index = selection.image_index;
    assert(selection.builtin_index == 0);
    assert(selection.uses_custom_gallery);

    assert(gallery_image_selection_for_date(2026, 7, 13, 1, 24, 7, &selection));
    assert(selection.image_index == (sunday_custom_index + 1) % 24);
    assert(selection.builtin_index == 1);
    assert(selection.uses_custom_gallery);

    assert(gallery_image_selection_for_date(2026, 12, 31, 4, 7, 7, &selection));
    int year_end_index = selection.image_index;
    assert(gallery_image_selection_for_date(2027, 1, 1, 5, 7, 7, &selection));
    assert(selection.image_index == (year_end_index + 1) % 7);

    assert(!gallery_image_selection_for_date(2026, 2, 29, 0, 0, 7, &selection));
    assert(!gallery_image_selection_for_date(2026, 7, 12, -1, 0, 7, &selection));
    assert(!gallery_image_selection_for_date(2026, 7, 12, 7, 0, 7, &selection));
    assert(!gallery_image_selection_for_date(2026, 7, 12, 0, -1, 7, &selection));
    assert(!gallery_image_selection_for_date(2026, 7, 12, 0, 0, 0, &selection));
    assert(!gallery_image_selection_for_date(2026, 7, 12, 0, 0, 7, nullptr));
    return 0;
}
