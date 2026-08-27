// 提供图片时钟按日期选择自定义/内置图库索引的纯逻辑。
#pragma once

#include <stdint.h>

struct GalleryImageSelection {
    int image_index;
    int builtin_index;
    bool uses_custom_gallery;
};

bool gallery_image_selection_for_date(int year,
                                      int month,
                                      int day,
                                      int weekday,
                                      int custom_image_count,
                                      int builtin_image_count,
                                      GalleryImageSelection *selection);
