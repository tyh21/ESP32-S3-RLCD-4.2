// 提供数组长度和固定 C 字符串表的通用编译期校验。
#pragma once

#include <stddef.h>

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

constexpr bool cstr_equal(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

constexpr const char *cstr_or_empty(const char *text)
{
    return text ? text : "";
}

constexpr size_t cstr_length(const char *text)
{
    size_t length = 0;
    if (!text) {
        return 0;
    }
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

template <typename T, size_t N>
constexpr bool cstr_array_nonempty(const T (&items)[N])
{
    for (const char *text : items) {
        if (!cstr_nonempty(text)) {
            return false;
        }
    }
    return true;
}

template <typename T, size_t N>
constexpr bool cstr_array_contains(const T (&items)[N], const char *needle)
{
    for (const char *text : items) {
        if (cstr_equal(text, needle)) {
            return true;
        }
    }
    return false;
}

template <typename T, size_t N>
constexpr bool cstr_array_unique(const T (&items)[N])
{
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = i + 1; j < N; ++j) {
            if (cstr_equal(items[i], items[j])) {
                return false;
            }
        }
    }
    return true;
}
