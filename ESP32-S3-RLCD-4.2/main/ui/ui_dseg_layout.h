// 计算双位数码字形缩放后的水平居中布局。
#pragma once

struct DsegPairLayout {
    int first_origin_x;
    int second_origin_x;
};

constexpr int dseg_scaled_position(int value, int scale_num, int scale_den)
{
    return scale_den > 0 ? (value * scale_num) / scale_den : 0;
}

constexpr int dseg_scaled_size(int value, int scale_num, int scale_den)
{
    return scale_den > 0 ? (value * scale_num + scale_den - 1) / scale_den : 0;
}

constexpr DsegPairLayout centered_dseg_pair_layout(int container_width,
                                                    int scale_num,
                                                    int scale_den,
                                                    int first_x_offset,
                                                    int first_width,
                                                    int first_x_advance,
                                                    int second_x_offset,
                                                    int second_width)
{
    if (scale_num <= 0 || scale_den <= 0) {
        return {0, 0};
    }
    const int second_unshifted_origin = dseg_scaled_position(first_x_advance,
                                                             scale_num,
                                                             scale_den);
    const int first_left = dseg_scaled_position(first_x_offset, scale_num, scale_den);
    const int second_left = second_unshifted_origin +
                            dseg_scaled_position(second_x_offset, scale_num, scale_den);
    const int left = first_left < second_left ? first_left : second_left;
    const int first_right = first_left + dseg_scaled_size(first_width, scale_num, scale_den);
    const int second_right = second_left + dseg_scaled_size(second_width, scale_num, scale_den);
    const int right = first_right > second_right ? first_right : second_right;
    const int first_origin = (container_width - (right - left)) / 2 - left;
    return {first_origin, first_origin + second_unshifted_origin};
}
