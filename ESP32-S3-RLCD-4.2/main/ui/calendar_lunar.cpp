// 计算公历、农历、节日和节气等日历页面数据。
#include "calendar_lunar.h"

#include "app_constexpr.h"
#include "app_text_format.h"
#include "app_time_constants.h"

#include "app_state.h"

struct LunarYearInfo {
    int year;
    uint32_t data;
};

struct CalendarFestivalRule {
    int month;
    int day;
    const char *name;
};

static constexpr LunarYearInfo kLunarYears[] = {
    {2023, 0x0d2b2},
    {2024, 0x0a950},
    {2025, 0x0b557},
    {2026, 0x056a0},
    {2027, 0x0a5b0},
    {2028, 0x152b5},
    {2029, 0x052b0},
    {2030, 0x0a930},
    {2031, 0x07954},
    {2032, 0x06aa0},
    {2033, 0x0ad50},
    {2034, 0x05b52},
    {2035, 0x04b60},
};

constexpr bool lunar_year_table_contiguous()
{
    for (size_t i = 1; i < array_count(kLunarYears); ++i) {
        if (kLunarYears[i].year != kLunarYears[i - 1].year + 1) {
            return false;
        }
    }
    return true;
}

static const char *const kLunarDayNames[] = {
    "",
    "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

static const char *const kLunarMonthNames[] = {
    "",
    "正月", "二月", "三月", "四月", "五月", "六月",
    "七月", "八月", "九月", "十月", "冬月", "腊月",
};

static const char *const kSolarTermNames[] = {
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分",
    "清明", "谷雨", "立夏", "小满", "芒种", "夏至",
    "小暑", "大暑", "立秋", "处暑", "白露", "秋分",
    "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};

static constexpr int kFirstGregorianMonth = 1;
static constexpr int kLastGregorianMonth = 12;
static constexpr int kFebruaryMonth = 2;
static constexpr int kCommonFebruaryDays = 28;
static constexpr int kLeapFebruaryDays = 29;
static constexpr int kFallbackGregorianMonthDays = 30;
static constexpr int kLeapYearCycle4 = 4;
static constexpr int kLeapYearCycle100 = 100;
static constexpr int kLeapYearCycle400 = 400;
static constexpr int kFirstLunarMonth = 1;
static constexpr int kLastLunarMonth = 12;
static constexpr int kFirstLunarDay = 1;
static constexpr int kLunarSmallMonthDays = 29;
static constexpr int kLunarLargeMonthDays = 30;
static constexpr int kLunarYearBaseDays = kLastLunarMonth * kLunarSmallMonthDays;
static constexpr uint32_t kLunarLeapMonthMask = 0x0f;
static constexpr uint32_t kLunarLeapMonthDaysMask = 0x10000;
static constexpr uint32_t kLunarMonthDaysBaseMask = 0x10000;
static constexpr int kLunarYearDaysFirstMask = 0x8000;
static constexpr int kLunarYearDaysLastMask = 0x8;
static constexpr int kLunarBaseYear = 2023;
static constexpr int kLunarBaseMonth = 1;
static constexpr int kLunarBaseDay = 22;
static constexpr int kSolarTermsPerMonth = 2;
static constexpr int kSolarTermBaseYear = 1900;
static constexpr double kSolarTermYearMs = 31556925974.7;
static constexpr double kSolarTermBaseMs = -2208491700000.0; // 1900-01-06 02:05 UTC
static constexpr double kMsPerMinute = 60000.0;
static constexpr double kMsPerDay = 86400000.0;
static constexpr const char *kCalendarLunarPlaceholder = "--";
static constexpr const char *kLunarMonthDisplayFormat = "%s%s";

static const int kGregorianMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static_assert(array_count(kGregorianMonthDays) == kLastGregorianMonth,
              "gregorian month days must cover January through December");

static const int kSolarTermMinutes[] = {
    0, 21208, 42467, 63836, 85337, 107014,
    128867, 150921, 173149, 195551, 218072, 240693,
    263343, 285989, 308563, 331033, 353350, 375494,
    397447, 419210, 440795, 462224, 483532, 504758,
};

static const CalendarFestivalRule kGregorianFestivals[] = {
    {1, 1, "元旦"},
    {2, 14, "情人节"},
    {3, 8, "妇女节"},
    {5, 1, "劳动节"},
    {6, 1, "儿童节"},
    {9, 10, "教师节"},
    {10, 1, "国庆"},
    {12, 25, "圣诞"},
};

static const CalendarFestivalRule kLunarFestivals[] = {
    {1, 1, "春节"},
    {1, 15, "元宵"},
    {5, 5, "端午"},
    {7, 7, "七夕"},
    {8, 15, "中秋"},
    {9, 9, "重阳"},
    {12, 8, "腊八"},
};
static_assert(array_count(kLunarYears) > 0, "lunar year table must not be empty");
static_assert(kLunarYears[0].year <= kMinValidYear &&
                  kLunarYears[array_count(kLunarYears) - 1].year >= kMaxValidYear,
              "lunar year table must cover the supported calendar range");
static_assert(lunar_year_table_contiguous(), "lunar year table must stay contiguous");
static_assert(array_count(kLunarDayNames) == static_cast<size_t>(kLunarLargeMonthDays + 1),
              "lunar day names must cover day zero through day thirty");
static_assert(array_count(kLunarMonthNames) == static_cast<size_t>(kLastLunarMonth + 1),
              "lunar month names must cover month zero through month twelve");
static_assert(array_count(kSolarTermNames) == kLastGregorianMonth * kSolarTermsPerMonth,
              "solar term names must cover two terms per month");
static_assert(array_count(kSolarTermMinutes) == kLastGregorianMonth * kSolarTermsPerMonth,
              "solar term minute offsets must cover two terms per month");

static int days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static void civil_from_days(int z, int *year, unsigned *month, unsigned *day)
{
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp + (mp < 10 ? 3 : -9);
    y += m <= 2;
    *year = y;
    *month = m;
    *day = d;
}

static const LunarYearInfo *find_lunar_year(int year)
{
    for (const auto &item : kLunarYears) {
        if (item.year == year) {
            return &item;
        }
    }
    return nullptr;
}

static bool is_gregorian_leap_year(int year)
{
    return (year % kLeapYearCycle4 == 0 && year % kLeapYearCycle100 != 0) ||
           (year % kLeapYearCycle400 == 0);
}

static int leap_month(uint32_t data)
{
    return (int)(data & kLunarLeapMonthMask);
}

static int leap_month_days(uint32_t data)
{
    if (leap_month(data) == 0) {
        return 0;
    }
    return (data & kLunarLeapMonthDaysMask) ? kLunarLargeMonthDays : kLunarSmallMonthDays;
}

static int lunar_month_days(uint32_t data, int month)
{
    return (data & (kLunarMonthDaysBaseMask >> month)) ? kLunarLargeMonthDays : kLunarSmallMonthDays;
}

static int lunar_year_days(uint32_t data)
{
    int days = kLunarYearBaseDays;
    for (int mask = kLunarYearDaysFirstMask; mask > kLunarYearDaysLastMask; mask >>= 1) {
        if (data & mask) {
            ++days;
        }
    }
    return days + leap_month_days(data);
}

int calendar_days_in_month(int year, int month)
{
    if (month < kFirstGregorianMonth || month > kLastGregorianMonth) {
        return kFallbackGregorianMonthDays;
    }
    if (month == kFebruaryMonth) {
        return is_gregorian_leap_year(year) ? kLeapFebruaryDays : kCommonFebruaryDays;
    }
    return kGregorianMonthDays[month - kFirstGregorianMonth];
}

int calendar_first_weekday(int year, int month)
{
    int days = days_from_civil(year, (unsigned)month, 1);
    int weekday = (days + 4) % 7;
    return weekday < 0 ? weekday + 7 : weekday;
}

static bool lunar_from_date(int year, int month, int day, CalendarDayInfo *info)
{
    int offset = days_from_civil(year, (unsigned)month, (unsigned)day) -
                 days_from_civil(kLunarBaseYear, kLunarBaseMonth, kLunarBaseDay);
    if (offset < 0) {
        return false;
    }

    int lunar_year = kLunarBaseYear;
    const LunarYearInfo *year_info = find_lunar_year(lunar_year);
    while (year_info) {
        int days = lunar_year_days(year_info->data);
        if (offset < days) {
            break;
        }
        offset -= days;
        ++lunar_year;
        year_info = find_lunar_year(lunar_year);
    }
    if (!year_info) {
        return false;
    }

    int lunar_month = kFirstLunarMonth;
    bool is_leap = false;
    int leap = leap_month(year_info->data);
    for (;;) {
        int days = is_leap ? leap_month_days(year_info->data) : lunar_month_days(year_info->data, lunar_month);
        if (offset < days) {
            break;
        }
        offset -= days;
        if (leap == lunar_month && !is_leap) {
            is_leap = true;
        } else {
            is_leap = false;
            ++lunar_month;
        }
        if (lunar_month > kLastLunarMonth) {
            return false;
        }
    }

    info->lunar_year = lunar_year;
    info->lunar_month = lunar_month;
    info->lunar_day = offset + 1;
    info->lunar_leap = is_leap;
    return true;
}

static const char *gregorian_festival(int month, int day)
{
    for (const auto &festival : kGregorianFestivals) {
        if (month == festival.month && day == festival.day) {
            return festival.name;
        }
    }
    return nullptr;
}

static const char *lunar_festival(int lunar_month, int lunar_day)
{
    for (const auto &festival : kLunarFestivals) {
        if (lunar_month == festival.month && lunar_day == festival.day) {
            return festival.name;
        }
    }
    return nullptr;
}

static void set_calendar_subtext(CalendarDayInfo *info, const char *text)
{
    strlcpy(info->subtext, text ? text : kCalendarLunarPlaceholder, sizeof(info->subtext));
}

static void set_calendar_lunar_month_subtext(CalendarDayInfo *info)
{
    int written = snprintf(info->subtext, sizeof(info->subtext), kLunarMonthDisplayFormat,
                           info->lunar_leap ? "闰" : "",
                           kLunarMonthNames[info->lunar_month]);
    if (app_text::format_failed(written, sizeof(info->subtext))) {
        set_calendar_subtext(info, nullptr);
    }
}

static const char *solar_term(int year, int month, int day)
{
    if (year < kMinValidYear || year > kMaxValidYear ||
        month < kFirstGregorianMonth || month > kLastGregorianMonth) {
        return nullptr;
    }
    int first = (month - kFirstGregorianMonth) * kSolarTermsPerMonth;
    for (int i = 0; i < kSolarTermsPerMonth; ++i) {
        int term = first + i;
        double ms = kSolarTermBaseMs + kSolarTermYearMs * (year - kSolarTermBaseYear) +
                    (double)kSolarTermMinutes[term] * kMsPerMinute;
        int days = (int)(ms / kMsPerDay);
        int ty = 0;
        unsigned tm = 0;
        unsigned td = 0;
        civil_from_days(days, &ty, &tm, &td);
        if (ty == year && (int)tm == month && (int)td == day) {
            return kSolarTermNames[term];
        }
    }
    return nullptr;
}

bool calendar_day_info(const struct tm &local, CalendarDayInfo *info)
{
    if (!info) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    info->year = local.tm_year + kTmYearOffset;
    info->month = local.tm_mon + kTmMonthOffset;
    info->day = local.tm_mday;
    if (info->year < kMinValidYear || info->year > kMaxValidYear) {
        set_calendar_subtext(info, nullptr);
        return false;
    }

    bool lunar_ok = lunar_from_date(info->year, info->month, info->day, info);
    const char *text = gregorian_festival(info->month, info->day);
    if (!text) {
        text = solar_term(info->year, info->month, info->day);
    }
    if (!text && lunar_ok && !info->lunar_leap) {
        text = lunar_festival(info->lunar_month, info->lunar_day);
    }
    if (!text && lunar_ok) {
        if (info->lunar_day == kFirstLunarDay &&
            info->lunar_month >= kFirstLunarMonth &&
            info->lunar_month <= kLastLunarMonth) {
            set_calendar_lunar_month_subtext(info);
            return true;
        }
        if (info->lunar_day >= kFirstLunarDay && info->lunar_day <= kLunarLargeMonthDays) {
            text = kLunarDayNames[info->lunar_day];
        }
    }
    set_calendar_subtext(info, text);
    return lunar_ok;
}
