// 验证双位数码字形缩放居中的普通、偏移和异常比例边界。
#include "ui_dseg_layout.h"

#include <assert.h>

int main()
{
    static_assert(dseg_scaled_position(11, 3, 4) == 8);
    static_assert(dseg_scaled_size(11, 3, 4) == 9);
    static_assert(dseg_scaled_position(-3, 3, 4) == -2);

    constexpr DsegPairLayout equal = centered_dseg_pair_layout(112,
                                                                3,
                                                                4,
                                                                0,
                                                                40,
                                                                44,
                                                                0,
                                                                40);
    static_assert(equal.first_origin_x == 24);
    static_assert(equal.second_origin_x == 57);

    constexpr DsegPairLayout offsets = centered_dseg_pair_layout(112,
                                                                  3,
                                                                  4,
                                                                  -2,
                                                                  37,
                                                                  42,
                                                                  3,
                                                                  35);
    static_assert(offsets.first_origin_x == 26);
    static_assert(offsets.second_origin_x == 57);

    constexpr DsegPairLayout invalid = centered_dseg_pair_layout(112,
                                                                  3,
                                                                  0,
                                                                  0,
                                                                  40,
                                                                  44,
                                                                  0,
                                                                  40);
    static_assert(invalid.first_origin_x == 0 && invalid.second_origin_x == 0);

    assert(equal.second_origin_x > equal.first_origin_x);
    return 0;
}
