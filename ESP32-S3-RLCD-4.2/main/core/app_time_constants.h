// 统一定义 C struct tm 与自然年月之间的标准换算偏移。
#pragma once

inline constexpr int kTmYearOffset = 1900;
inline constexpr int kTmMonthOffset = 1;

static_assert(kTmYearOffset == 1900,
              "struct tm year offset must stay 1900");
static_assert(kTmMonthOffset == 1,
              "struct tm month offset must stay 1");
