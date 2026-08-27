// 声明温湿度小时历史的新旧 NVS 格式和纯槽位换算规则。
#pragma once

#include "app_state.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace sensor_history_format {

inline constexpr uint16_t kHourlyHistoryMetaVersion = 2;
inline constexpr uint16_t kLegacyHourlyHistoryVersion = 1;
namespace detail {
inline constexpr int kSecondsPerMinute = 60;
inline constexpr int kMinutesPerHour = 60;
inline constexpr int kSecondsPerHour = kMinutesPerHour * kSecondsPerMinute;
} // namespace detail

struct HourlySensorHistoryMeta {
    uint32_t magic = kHourlyHistoryMagic;
    uint16_t version = kHourlyHistoryMetaVersion;
    uint16_t count = kHourlyHistoryCount;
    int64_t last_saved_at = 0;
};

struct LegacyHourlySensorHistoryBlob {
    uint32_t magic = kHourlyHistoryMagic;
    uint16_t version = kLegacyHourlyHistoryVersion;
    uint16_t count = kLegacyHourlyHistoryCount;
    HourlySensorSample samples[kLegacyHourlyHistoryCount] = {};
};

static_assert(sizeof(HourlySensorSample) == 24,
              "hourly sensor sample wire size must remain 24 bytes");
static_assert(offsetof(HourlySensorHistoryMeta, last_saved_at) == 8 &&
                  sizeof(HourlySensorHistoryMeta) == 16,
              "hourly history meta wire layout must remain stable");
static_assert(offsetof(LegacyHourlySensorHistoryBlob, samples) == 8 &&
                  sizeof(LegacyHourlySensorHistoryBlob) ==
                      8 + sizeof(HourlySensorSample) * kLegacyHourlyHistoryCount,
              "legacy hourly history wire layout must remain stable");

inline bool hourly_meta_valid(const HourlySensorHistoryMeta &meta, size_t meta_len)
{
    return meta_len == sizeof(meta) &&
           meta.magic == kHourlyHistoryMagic &&
           meta.version == kHourlyHistoryMetaVersion &&
           meta.count == kHourlyHistoryCount;
}

inline bool legacy_history_valid(const LegacyHourlySensorHistoryBlob &legacy, size_t legacy_len)
{
    return legacy_len == sizeof(legacy) &&
           legacy.magic == kHourlyHistoryMagic &&
           legacy.version == kLegacyHourlyHistoryVersion &&
           legacy.count == kLegacyHourlyHistoryCount;
}

inline bool hourly_index_valid(int index)
{
    return index >= 0 && index < kHourlyHistoryCount;
}

inline int hourly_slot_index_for_time(time_t hour_start)
{
    int index = static_cast<int>((hour_start / detail::kSecondsPerHour) %
                                 kHourlyHistoryCount);
    if (index < 0) {
        index += kHourlyHistoryCount;
    }
    return index;
}

} // namespace sensor_history_format
