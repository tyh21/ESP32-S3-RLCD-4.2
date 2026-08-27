// 验证温湿度小时历史 wire 格式和环形槽位换算保持兼容。
#include "sensor_history_format.h"

#include <assert.h>
#include <stddef.h>

int main()
{
    using namespace sensor_history_format;

    HourlySensorHistoryMeta meta = {};
    assert(hourly_meta_valid(meta, sizeof(meta)));
    assert(!hourly_meta_valid(meta, sizeof(meta) - 1));
    meta.magic = 0;
    assert(!hourly_meta_valid(meta, sizeof(meta)));
    meta = {};
    meta.version = kLegacyHourlyHistoryVersion;
    assert(!hourly_meta_valid(meta, sizeof(meta)));
    meta = {};
    meta.count = kHourlyHistoryCount - 1;
    assert(!hourly_meta_valid(meta, sizeof(meta)));

    LegacyHourlySensorHistoryBlob legacy = {};
    assert(legacy_history_valid(legacy, sizeof(legacy)));
    assert(!legacy_history_valid(legacy, sizeof(legacy) - 1));
    legacy.magic = 0;
    assert(!legacy_history_valid(legacy, sizeof(legacy)));
    legacy = {};
    legacy.version = kHourlyHistoryMetaVersion;
    assert(!legacy_history_valid(legacy, sizeof(legacy)));
    legacy = {};
    legacy.count = kLegacyHourlyHistoryCount - 1;
    assert(!legacy_history_valid(legacy, sizeof(legacy)));

    static_assert(offsetof(HourlySensorHistoryMeta, magic) == 0,
                  "hourly meta magic must remain first");
    static_assert(offsetof(HourlySensorHistoryMeta, last_saved_at) >= 8,
                  "hourly meta timestamp must follow the wire header");
    static_assert(offsetof(LegacyHourlySensorHistoryBlob, samples) == 8,
                  "legacy samples must follow the eight-byte wire header");

    assert(!hourly_index_valid(-1));
    assert(hourly_index_valid(0));
    assert(hourly_index_valid(kHourlyHistoryCount - 1));
    assert(!hourly_index_valid(kHourlyHistoryCount));

    constexpr time_t kSecondsPerHour = 3600;
    assert(hourly_slot_index_for_time(0) == 0);
    assert(hourly_slot_index_for_time(kSecondsPerHour) == 1);
    assert(hourly_slot_index_for_time((kHourlyHistoryCount - 1) * kSecondsPerHour) ==
           kHourlyHistoryCount - 1);
    assert(hourly_slot_index_for_time(kHourlyHistoryCount * kSecondsPerHour) == 0);
    assert(hourly_slot_index_for_time(-kSecondsPerHour) == kHourlyHistoryCount - 1);

    return 0;
}
