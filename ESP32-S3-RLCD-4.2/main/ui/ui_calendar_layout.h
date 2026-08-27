// 提供当月日历固定五行视口和第六行动态上移的纯布局计算。
#pragma once

#include <stddef.h>
#include <time.h>

inline constexpr int kCalendarWeekdayCount = 7;
inline constexpr int kCalendarVisibleRowCount = 5;

struct CalendarMonthLayout {
    int first_weekday = 0;
    int day_count = 0;
    int row_offset = 0;
};

bool calculate_calendar_month_layout(int first_weekday,
                                     int day_count,
                                     int today,
                                     CalendarMonthLayout *out);

bool calendar_day_display_position(const CalendarMonthLayout &layout,
                                   int day,
                                   int *display_row,
                                   int *column);
int calendar_month_key(const struct tm &local);
void format_calendar_day_text(char *out, size_t out_len, int day);
