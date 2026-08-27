// 将 LVGL 像素写入 RLCD，并选择局部或全屏刷新及记录诊断统计。
#include "ui_display_flush.h"

#include "app_constexpr.h"
#include "app_state.h"
#include "ota_runtime_state.h"

namespace {
constexpr uint32_t kDisplayFullReasonSingleWide = 1U << 0;
constexpr uint32_t kDisplayFullReasonTooManyRanges = 1U << 1;
constexpr uint32_t kDisplayFullReasonCoveredWide = 1U << 2;
constexpr uint16_t kRlcdBlackThreshold = 0xC618;
#define DISPLAY_FLUSH_DIAG_LOG_FORMAT "display flush diag: page=%d partial=%lu ranges=%lu full=%lu reason_single=%lu reason_covered=%lu reason_ranges=%lu"
#define DISPLAY_FULL_REASON_OVERLAP_ASSERT "display full-refresh reason bits must not overlap"

constexpr const char *kDisplayFlushLogTexts[] = {
    DISPLAY_FLUSH_DIAG_LOG_FORMAT,
};

struct FlushRange {
    int x1;
    int x2;
};

constexpr bool flush_ranges_touch(const FlushRange &range, int x1, int x2)
{
    return x1 <= range.x2 + kFlushRangeMergeGap &&
           x2 >= range.x1 - kFlushRangeMergeGap;
}

constexpr bool add_merged_flush_range(FlushRange *ranges, int *range_count, int x1, int x2)
{
    if (!ranges || !range_count || *range_count < 0 || *range_count > kMaxFlushRanges || x1 > x2) {
        return false;
    }
    int merged_x1 = x1;
    int merged_x2 = x2;
    for (int i = 0; i < *range_count;) {
        if (!flush_ranges_touch(ranges[i], merged_x1, merged_x2)) {
            ++i;
            continue;
        }
        if (ranges[i].x1 < merged_x1) {
            merged_x1 = ranges[i].x1;
        }
        if (ranges[i].x2 > merged_x2) {
            merged_x2 = ranges[i].x2;
        }
        --(*range_count);
        ranges[i] = ranges[*range_count];
        i = 0;
    }
    if (*range_count >= kMaxFlushRanges) {
        return false;
    }
    ranges[(*range_count)++] = {merged_x1, merged_x2};
    return true;
}

constexpr bool bridged_flush_ranges_merge_once()
{
    FlushRange ranges[kMaxFlushRanges] = {{0, 10}, {20, 30}};
    int range_count = 2;
    return add_merged_flush_range(ranges, &range_count, 8, 22) &&
           range_count == 1 &&
           ranges[0].x1 == 0 &&
           ranges[0].x2 == 30;
}

static_assert(cstr_array_nonempty(kDisplayFlushLogTexts), "display flush log texts must be non-empty");
static_assert(kDisplayFullReasonSingleWide != 0, "display full-refresh single-wide reason bit must be nonzero");
static_assert(kDisplayFullReasonTooManyRanges != 0, "display full-refresh range-count reason bit must be nonzero");
static_assert(kDisplayFullReasonCoveredWide != 0, "display full-refresh covered-width reason bit must be nonzero");
static_assert((kDisplayFullReasonSingleWide & kDisplayFullReasonTooManyRanges) == 0,
              DISPLAY_FULL_REASON_OVERLAP_ASSERT);
static_assert((kDisplayFullReasonSingleWide & kDisplayFullReasonCoveredWide) == 0,
              DISPLAY_FULL_REASON_OVERLAP_ASSERT);
static_assert((kDisplayFullReasonTooManyRanges & kDisplayFullReasonCoveredWide) == 0,
              DISPLAY_FULL_REASON_OVERLAP_ASSERT);
static_assert(kRlcdBlackThreshold > 0, "RLCD black threshold must be nonzero");
static_assert(kMaxFlushRanges > 0, "display flush range capacity must be positive");
static_assert(kFlushRangeMergeGap >= 0, "display flush range merge gap must be non-negative");
static_assert(bridged_flush_ranges_merge_once(),
              "bridging flush ranges must collapse into one covered interval");
} // namespace

void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    static FlushRange ranges[kMaxFlushRanges];
    static int range_count = 0;
    static bool force_full_refresh = false;
    static uint32_t full_reason_mask = 0;
    static uint32_t partial_cycles = 0;
    static uint32_t partial_ranges = 0;
    static uint32_t full_cycles = 0;
    static uint32_t full_single_wide = 0;
    static uint32_t full_covered_wide = 0;
    static uint32_t full_too_many_ranges = 0;
    static TickType_t last_diag_tick = 0;
    static int last_diag_page = -1;

    if (ota_runtime_reboot_pending_load()) {
        range_count = 0;
        force_full_refresh = false;
        full_reason_mask = 0;
        lv_disp_flush_ready(drv);
        return;
    }

    int clipped_x1 = area->x1 < 0 ? 0 : area->x1;
    int clipped_x2 = area->x2 >= kDisplayWidth ? kDisplayWidth - 1 : area->x2;
    int clipped_y1 = area->y1 < 0 ? 0 : area->y1;
    int clipped_y2 = area->y2 >= kDisplayHeight ? kDisplayHeight - 1 : area->y2;
    bool touches_visible_area = clipped_x1 <= clipped_x2 && clipped_y1 <= clipped_y2;
    if (touches_visible_area) {
        int area_x1 = clipped_x1;
        int area_x2 = clipped_x2;
        area_x1 &= ~1;
        area_x2 |= 1;
        if (area_x2 >= kDisplayWidth) {
            area_x2 = kDisplayWidth - 1;
        }
        if (area_x2 - area_x1 + 1 >= kDisplayPartialMaxWidth) {
            force_full_refresh = true;
            full_reason_mask |= kDisplayFullReasonSingleWide;
        } else if (!add_merged_flush_range(ranges, &range_count, area_x1, area_x2)) {
            force_full_refresh = true;
            full_reason_mask |= kDisplayFullReasonTooManyRanges;
        }
    }

    if (touches_visible_area) {
        int area_width = area->x2 - area->x1 + 1;
        uint16_t *buffer = (uint16_t *)color_map;
        for (int y = clipped_y1; y <= clipped_y2; ++y) {
            uint16_t *row = buffer + (y - area->y1) * area_width + (clipped_x1 - area->x1);
            for (int x = clipped_x1; x <= clipped_x2; ++x) {
                uint8_t color = (*row < kRlcdBlackThreshold) ? ColorBlack : ColorWhite;
                g_display.RLCD_SetPixel(x, y, color);
                ++row;
            }
        }
    }
    if (lv_disp_flush_is_last(drv)) {
        int covered_width = 0;
        for (int i = 0; i < range_count; ++i) {
            covered_width += ranges[i].x2 - ranges[i].x1 + 1;
        }
        bool covered_wide = covered_width >= kDisplayPartialMaxWidth;
        if (covered_wide) {
            full_reason_mask |= kDisplayFullReasonCoveredWide;
        }
        if (force_full_refresh || covered_wide) {
            ++full_cycles;
            if (full_reason_mask & kDisplayFullReasonSingleWide) {
                ++full_single_wide;
            }
            if (full_reason_mask & kDisplayFullReasonTooManyRanges) {
                ++full_too_many_ranges;
            }
            if (full_reason_mask & kDisplayFullReasonCoveredWide) {
                ++full_covered_wide;
            }
            g_display.RLCD_Display();
        } else if (range_count > 0) {
            ++partial_cycles;
            partial_ranges += range_count;
            for (int i = 0; i < range_count; ++i) {
                g_display.RLCD_DisplayXRange(ranges[i].x1, ranges[i].x2);
            }
        }
        TickType_t now_tick = xTaskGetTickCount();
        int active_page = active_work_page_load();
        bool page_changed = last_diag_page != active_page;
        bool diag_due = last_diag_tick == 0 ||
                        now_tick - last_diag_tick >= pdMS_TO_TICKS(kDisplayFlushDiagIntervalMs) ||
                        page_changed;
        OtaRuntimeSnapshot ota;
        ota_runtime_snapshot_load(&ota);
        if (diag_due && ota.state != kOtaUpdating && !ota.reboot_pending) {
            ESP_LOGI(TAG,
                     DISPLAY_FLUSH_DIAG_LOG_FORMAT,
                     active_page,
                     (unsigned long)partial_cycles,
                     (unsigned long)partial_ranges,
                     (unsigned long)full_cycles,
                     (unsigned long)full_single_wide,
                     (unsigned long)full_covered_wide,
                     (unsigned long)full_too_many_ranges);
            partial_cycles = 0;
            partial_ranges = 0;
            full_cycles = 0;
            full_single_wide = 0;
            full_covered_wide = 0;
            full_too_many_ranges = 0;
            last_diag_tick = now_tick;
            last_diag_page = active_page;
        }
        range_count = 0;
        force_full_refresh = false;
        full_reason_mask = 0;
    }
    lv_disp_flush_ready(drv);
}
