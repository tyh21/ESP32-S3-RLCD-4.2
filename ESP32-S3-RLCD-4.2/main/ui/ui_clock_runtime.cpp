// 刷新天气时钟运行期时间、日期、秒进度和整点提醒。
#include "ui_views.h"

#include "audio_services.h"
#include "ui_clock_time.h"
#include "ui_draw_cache.h"

namespace {

constexpr size_t kClockDateTextSize = 48;
int s_last_ui_second = -1;
int s_last_ui_minute = -1;
int s_last_ui_date_key = -1;
int s_last_ui_date_page = -1;
int s_last_second_progress_filled = -1;

} // namespace

void invalidate_clock_time_draw_cache()
{
    s_last_ui_second = -1;
    s_last_ui_minute = -1;
    invalidate_clock_date_draw_cache();
}

void invalidate_clock_date_draw_cache()
{
    s_last_ui_date_key = -1;
    s_last_ui_date_page = -1;
}

void invalidate_clock_second_progress_draw_cache()
{
    s_last_second_progress_filled = -1;
}

bool update_time_ui(const struct tm &local,
                    bool clock_page_active,
                    int active_work_page)
{
    bool changed = false;
    static int last_chime_hour_key = -1;
    ClockUiTimeSnapshot time_snapshot = clock_ui_time_snapshot(local);
    if (clock_page_active && time_snapshot.minute_key != s_last_ui_minute) {
        draw_time_canvas(local);
        s_last_ui_minute = time_snapshot.minute_key;
        changed = true;
    }
    if (clock_page_active &&
        !battery_low_mode_load() &&
        local.tm_sec != s_last_ui_second) {
        draw_second_canvas(local);
        draw_status_gif_frame(local.tm_sec % STATUS_GIF_FRAME_COUNT);
        update_progress_canvas(g_second_progress_canvas,
                               local.tm_sec + 1,
                               &s_last_second_progress_filled);
        s_last_ui_second = local.tm_sec;
        changed = true;
    }

    int date_page = (active_work_page == kWorkPageWeatherClock ||
                     battery_low_mode_load() ||
                     setup_portal_active_load())
                        ? kWorkPageWeatherClock
                        : active_work_page;
    if (time_snapshot.date_key != s_last_ui_date_key ||
        date_page != s_last_ui_date_page) {
        char date[kClockDateTextSize] = {};
        format_clock_date_text(date,
                               sizeof(date),
                               local,
                               time_snapshot.weekday);
        WorkPageStatusLabels labels = get_work_page_status_labels(date_page);
        changed |= set_label_text_if_changed(labels.date, date);
        s_last_ui_date_key = time_snapshot.date_key;
        s_last_ui_date_page = date_page;
    }

    bool chime_enabled = g_hourly_chime_enabled || g_hourly_chime_all_day;
    if (clock_hourly_chime_due(local,
                               time_snapshot,
                               chime_enabled,
                               battery_low_mode_load(),
                               last_chime_hour_key)) {
        last_chime_hour_key = time_snapshot.hour_key;
        play_hourly_chime(local.tm_hour);
    }
    return changed;
}
