#include "audio_wave_ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "board_lvgl.h"
#include "board_audio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "audio_wave_ui";

#ifndef AUDIO_WAVE_UI_ENABLE_STATS_LOG
#define AUDIO_WAVE_UI_ENABLE_STATS_LOG 0
#endif

#define AUDIO_UI_TASK_STACK_SIZE       (8 * 1024)
#define AUDIO_UI_TASK_PRIORITY         4
#define AUDIO_UI_TASK_CORE             1

#define AUDIO_UI_SAMPLE_RATE_HZ        16000
#define AUDIO_UI_CHANNEL_COUNT         2
#define AUDIO_UI_BITS_PER_SAMPLE       32
#define AUDIO_UI_READ_FRAMES           256
#define AUDIO_UI_WAVE_RANGE            100
#define AUDIO_UI_FREQ_BARS             16
#define AUDIO_UI_LEVEL_BARS            48
#define AUDIO_UI_LOG_INTERVAL_MS       1000
#define AUDIO_UI_REFRESH_INTERVAL_MS   120
#define AUDIO_UI_ENVELOPE_FULL_SCALE   5000
#define AUDIO_UI_FREQ_FULL_SCALE       2200
#define AUDIO_UI_PEAK_DECAY            2

static lv_obj_t *s_freq_chart;
static lv_obj_t *s_level_chart;
static lv_obj_t *s_info_label;
static lv_chart_series_t *s_freq_series;
static lv_chart_series_t *s_level_series;
static bool s_started;

static int32_t pcm32_to_pcm16(int32_t sample)
{
    /*
     * ES7210 在当前板子上按 32 bit 读取更稳定，有效音频在高 16 bit。
     * 播放端已经改成懒加载，因此这里不再让 ES8311 影响麦克风读取格式。
     */
    return sample >> 16;
}

static int32_t clamp_chart_value(int32_t value)
{
    if (value > AUDIO_UI_WAVE_RANGE) {
        return AUDIO_UI_WAVE_RANGE;
    }
    if (value < 0) {
        return 0;
    }
    return value;
}

static int32_t envelope_to_chart_value(int32_t value)
{
    return clamp_chart_value(value * AUDIO_UI_WAVE_RANGE / AUDIO_UI_ENVELOPE_FULL_SCALE);
}

static int32_t freq_to_chart_value(float value)
{
    int32_t chart_value = (int32_t)(value * (float)AUDIO_UI_WAVE_RANGE / (float)AUDIO_UI_FREQ_FULL_SCALE);
    return clamp_chart_value(chart_value);
}

static float goertzel_magnitude(const float *samples, size_t count, int bin)
{
    const float omega = 2.0f * (float)M_PI * (float)bin / (float)count;
    const float coeff = 2.0f * cosf(omega);
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;

    for (size_t i = 0; i < count; i++) {
        q0 = coeff * q1 - q2 + samples[i];
        q2 = q1;
        q1 = q0;
    }

    float power = q1 * q1 + q2 * q2 - coeff * q1 * q2;
    if (power < 0.0f) {
        power = 0.0f;
    }
    return sqrtf(power) / (float)count;
}

static void fill_chart_with_zero(lv_obj_t *chart, lv_chart_series_t *series, int count)
{
    for (int i = 0; i < count; i++) {
        lv_chart_set_next_value(chart, series, 0);
    }
}

static lv_obj_t *create_chart(lv_obj_t *parent,
                              const char *title,
                              int y_offset,
                              int height,
                              int points,
                              lv_chart_type_t type,
                              lv_chart_series_t **out_series)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 12, y_offset);

    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_set_size(chart, 376, height);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, y_offset + 22);
    lv_obj_set_style_bg_color(chart, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(chart, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, lv_color_black(), LV_PART_ITEMS);
    lv_obj_set_style_line_width(chart, 1, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(chart, lv_color_black(), LV_PART_ITEMS);

    lv_chart_set_type(chart, type);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart, points);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, AUDIO_UI_WAVE_RANGE);
    lv_chart_set_div_line_count(chart, 3, 5);

    *out_series = lv_chart_add_series(chart, lv_color_black(), LV_CHART_AXIS_PRIMARY_Y);
    fill_chart_with_zero(chart, *out_series, points);
    lv_chart_refresh(chart);

    return chart;
}

static esp_err_t create_audio_screen(void)
{
    if (!board_lvgl_lock(-1)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Audio Analysis");
    lv_obj_set_style_text_color(title, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    s_info_label = lv_label_create(screen);
    lv_label_set_text(s_info_label, "Mono L:000 P:000");
    lv_obj_set_style_text_color(s_info_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(s_info_label, LV_ALIGN_TOP_LEFT, 12, 26);

    s_freq_chart = create_chart(screen, "Frequency", 48, 96, AUDIO_UI_FREQ_BARS, LV_CHART_TYPE_BAR, &s_freq_series);
    s_level_chart = create_chart(screen, "Loudness History", 176, 88, AUDIO_UI_LEVEL_BARS, LV_CHART_TYPE_BAR, &s_level_series);

    lv_scr_load(screen);
    board_lvgl_unlock();
    return ESP_OK;
}

static void calculate_frequency_bars(const float *mono, size_t frames_read, int32_t *freq_values)
{
    /*
     * 256 点采样在 16 kHz 下，频率分辨率是 62.5 Hz。
     * 这里把 1~80 bin 大致压成 16 个频段，作为测试界面的频域能量图。
     */
    for (int bar = 0; bar < AUDIO_UI_FREQ_BARS; bar++) {
        int start_bin = 1 + bar * 5;
        int end_bin = start_bin + 4;
        float sum = 0.0f;

        for (int bin = start_bin; bin <= end_bin; bin++) {
            sum += goertzel_magnitude(mono, frames_read, bin);
        }

        freq_values[bar] = freq_to_chart_value(sum / 5.0f);
    }
}

static void audio_wave_task(void *arg)
{
    (void)arg;

    const size_t sample_count = AUDIO_UI_READ_FRAMES * AUDIO_UI_CHANNEL_COUNT;
    int32_t *pcm = (int32_t *)heap_caps_malloc(sample_count * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PCM buffer");
        vTaskDelete(NULL);
        return;
    }

    float mono[AUDIO_UI_READ_FRAMES];
    int32_t freq_values[AUDIO_UI_FREQ_BARS];
    TickType_t last_log_tick = 0;
    TickType_t last_ui_tick = 0;
    int32_t peak_hold = 0;

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = board_audio_read_mic(pcm, sample_count * sizeof(int32_t), &bytes_read, 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Read microphone failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        size_t frames_read = bytes_read / (sizeof(int32_t) * AUDIO_UI_CHANNEL_COUNT);
        if (frames_read == 0 || frames_read > AUDIO_UI_READ_FRAMES) {
            continue;
        }

        int32_t left_samples[AUDIO_UI_READ_FRAMES];
        int32_t right_samples[AUDIO_UI_READ_FRAMES];
        int64_t lr_sum[AUDIO_UI_CHANNEL_COUNT] = {0};

        for (size_t i = 0; i < frames_read; i++) {
            int32_t left = pcm32_to_pcm16(pcm[i * AUDIO_UI_CHANNEL_COUNT]);
            int32_t right = pcm32_to_pcm16(pcm[i * AUDIO_UI_CHANNEL_COUNT + 1]);
            left_samples[i] = left;
            right_samples[i] = right;
            lr_sum[0] += left;
            lr_sum[1] += right;
        }

        int32_t lr_avg[AUDIO_UI_CHANNEL_COUNT] = {
            (int32_t)(lr_sum[0] / (int64_t)frames_read),
            (int32_t)(lr_sum[1] / (int64_t)frames_read),
        };
        int32_t lr_peak[AUDIO_UI_CHANNEL_COUNT] = {0};
        int64_t lr_abs_sum[AUDIO_UI_CHANNEL_COUNT] = {0};

        for (size_t i = 0; i < frames_read; i++) {
            int32_t left_centered = left_samples[i] - lr_avg[0];
            int32_t right_centered = right_samples[i] - lr_avg[1];
            int32_t left_abs = left_centered < 0 ? -left_centered : left_centered;
            int32_t right_abs = right_centered < 0 ? -right_centered : right_centered;

            if (left_abs > lr_peak[0]) {
                lr_peak[0] = left_abs;
            }
            if (right_abs > lr_peak[1]) {
                lr_peak[1] = right_abs;
            }
            lr_abs_sum[0] += left_abs;
            lr_abs_sum[1] += right_abs;
        }

        int32_t left_avg_abs = (int32_t)(lr_abs_sum[0] / (int64_t)frames_read);
        int32_t right_avg_abs = (int32_t)(lr_abs_sum[1] / (int64_t)frames_read);

        /*
         * 自动选择有效通道。
         * 如果一个通道几乎没有数据，另一个通道有明显变化，就选有数据的通道。
         * 如果两路都有效且幅度接近，再合成 mono。
         */
        enum {
            MONO_SOURCE_MIX,
            MONO_SOURCE_LEFT,
            MONO_SOURCE_RIGHT,
        } mono_source = MONO_SOURCE_MIX;

        const int32_t active_threshold = 20;
        if (left_avg_abs < active_threshold && right_avg_abs >= active_threshold) {
            mono_source = MONO_SOURCE_RIGHT;
        } else if (right_avg_abs < active_threshold && left_avg_abs >= active_threshold) {
            mono_source = MONO_SOURCE_LEFT;
        } else if (left_avg_abs > right_avg_abs * 2) {
            mono_source = MONO_SOURCE_LEFT;
        } else if (right_avg_abs > left_avg_abs * 2) {
            mono_source = MONO_SOURCE_RIGHT;
        }

        int32_t mono_min = INT32_MAX;
        int32_t mono_max = INT32_MIN;
        int32_t mono_peak = 0;
        int64_t mono_abs_sum = 0;

        for (size_t i = 0; i < frames_read; i++) {
            int32_t left_centered = left_samples[i] - lr_avg[0];
            int32_t right_centered = right_samples[i] - lr_avg[1];
            int32_t centered = 0;

            if (mono_source == MONO_SOURCE_LEFT) {
                centered = left_centered;
            } else if (mono_source == MONO_SOURCE_RIGHT) {
                centered = right_centered;
            } else {
                centered = (left_centered + right_centered) / 2;
            }

            int32_t abs_sample = centered < 0 ? -centered : centered;
            mono[i] = (float)centered;

            if (centered < mono_min) {
                mono_min = centered;
            }
            if (centered > mono_max) {
                mono_max = centered;
            }
            if (abs_sample > mono_peak) {
                mono_peak = abs_sample;
            }
            mono_abs_sum += abs_sample;
        }

        int32_t mono_avg_abs = (int32_t)(mono_abs_sum / (int64_t)frames_read);
        int32_t level_value = envelope_to_chart_value(mono_avg_abs);
        int32_t peak_value = envelope_to_chart_value(mono_peak);

        if (peak_value > peak_hold) {
            peak_hold = peak_value;
        } else {
            peak_hold = clamp_chart_value(peak_hold - AUDIO_UI_PEAK_DECAY);
        }

        TickType_t now = xTaskGetTickCount();
#if AUDIO_WAVE_UI_ENABLE_STATS_LOG
        if ((now - last_log_tick) >= pdMS_TO_TICKS(AUDIO_UI_LOG_INTERVAL_MS)) {
            last_log_tick = now;
            ESP_LOGI(TAG, "Audio stats: L avg_abs=%ld peak=%ld | R avg_abs=%ld peak=%ld | Mono source=%s min=%ld max=%ld avg_abs=%ld peak=%ld level=%ld",
                     (long)left_avg_abs, (long)lr_peak[0],
                     (long)right_avg_abs, (long)lr_peak[1],
                     mono_source == MONO_SOURCE_LEFT ? "L" : (mono_source == MONO_SOURCE_RIGHT ? "R" : "MIX"),
                     (long)mono_min, (long)mono_max,
                     (long)mono_avg_abs, (long)mono_peak, (long)level_value);
        }
#else
        (void)last_log_tick;
#endif

        if ((now - last_ui_tick) < pdMS_TO_TICKS(AUDIO_UI_REFRESH_INTERVAL_MS)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        last_ui_tick = now;

        calculate_frequency_bars(mono, frames_read, freq_values);

        if (board_lvgl_lock(50)) {
            lv_label_set_text_fmt(s_info_label,
                                  "Mono %s L:%03ld P:%03ld",
                                  mono_source == MONO_SOURCE_LEFT ? "L" : (mono_source == MONO_SOURCE_RIGHT ? "R" : "MIX"),
                                  (long)level_value,
                                  (long)peak_hold);

            for (int i = 0; i < AUDIO_UI_FREQ_BARS; i++) {
                lv_chart_set_series_value_by_id(s_freq_chart, s_freq_series, i, freq_values[i]);
            }

            lv_chart_set_next_value(s_level_chart, s_level_series, level_value);

            lv_chart_refresh(s_freq_chart);
            lv_chart_refresh(s_level_chart);
            board_lvgl_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t audio_wave_ui_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    board_audio_config_t audio_config = {
        .sample_rate_hz = AUDIO_UI_SAMPLE_RATE_HZ,
        .channel_count = AUDIO_UI_CHANNEL_COUNT,
        .bits_per_sample = AUDIO_UI_BITS_PER_SAMPLE,
        .mic_gain_db = 35.0f,
        .speaker_volume = 60,
    };

    esp_err_t ret = board_audio_init(&audio_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = create_audio_screen();
    if (ret != ESP_OK) {
        return ret;
    }

    BaseType_t task_ret = xTaskCreatePinnedToCore(
        audio_wave_task,
        "audio_wave_ui",
        AUDIO_UI_TASK_STACK_SIZE,
        NULL,
        AUDIO_UI_TASK_PRIORITY,
        NULL,
        AUDIO_UI_TASK_CORE
    );
    if (task_ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "Audio analysis UI started");
    return ESP_OK;
}
