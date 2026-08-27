// 实现图片时钟图库的每日轮换和自定义资源失败后的内置索引映射。
#include "ui_gallery_selection.h"

namespace {
constexpr int kWeekdayCount = 7;
constexpr int kFirstSupportedYear = 1970;
constexpr int kLastSupportedYear = 9999;

constexpr bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

constexpr int days_in_month(int year, int month)
{
    constexpr int kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    return month == 2 && is_leap_year(year) ? 29 : kMonthDays[month - 1];
}

constexpr bool valid_date(int year, int month, int day)
{
    return year >= kFirstSupportedYear && year <= kLastSupportedYear &&
           day >= 1 && day <= days_in_month(year, month);
}

// Gregorian civil date to a monotonic day number. Only the relative sequence
// matters here, so the epoch offset is intentionally omitted.
int64_t civil_day_number(int year, int month, int day)
{
    year -= month <= 2;
    const int era = year / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned shifted_month = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned day_of_year = (153U * shifted_month + 2U) / 5U +
                                 static_cast<unsigned>(day - 1);
    const unsigned day_of_era = year_of_era * 365U + year_of_era / 4U -
                                year_of_era / 100U + day_of_year;
    return static_cast<int64_t>(era) * 146097LL + day_of_era;
}

int positive_mod(int64_t value, int divisor)
{
    int remainder = static_cast<int>(value % divisor);
    return remainder < 0 ? remainder + divisor : remainder;
}
}

bool gallery_image_selection_for_date(int year,
                                      int month,
                                      int day,
                                      int weekday,
                                      int custom_image_count,
                                      int builtin_image_count,
                                      GalleryImageSelection *selection)
{
    if (!selection || !valid_date(year, month, day) ||
        weekday < 0 || weekday >= kWeekdayCount ||
        custom_image_count < 0 || builtin_image_count <= 0) {
        return false;
    }
    selection->uses_custom_gallery = custom_image_count > 0;
    selection->builtin_index = weekday % builtin_image_count;
    selection->image_index = selection->uses_custom_gallery
                                 ? positive_mod(civil_day_number(year, month, day), custom_image_count)
                                 : selection->builtin_index;
    return true;
}
