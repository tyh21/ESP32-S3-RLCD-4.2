#include "gbc_emu.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "board_rlcd.h"
#include "board_speaker.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gnuboy.h"

static const char *TAG = "gbc_emu";

#ifndef GBC_EMU_ENABLE_PERF_LOG
#define GBC_EMU_ENABLE_PERF_LOG 0
#endif

#define GBC_EMU_TASK_STACK_SIZE      (16 * 1024)
#define GBC_EMU_TASK_PRIORITY        6
#define GBC_EMU_TASK_CORE            1
#define GBC_EMU_FRAME_US             16742
#define GBC_EMU_AUDIO_SAMPLE_RATE    24000
#define GBC_EMU_AUDIO_BUFFER_SAMPLES 1024
#define GBC_EMU_AUDIO_CHANNELS       2
#define GBC_EMU_AUDIO_BITS           16
#define GBC_EMU_AUDIO_VOLUME         75
#define GBC_EMU_AUDIO_GAIN_NUM       2
#define GBC_EMU_AUDIO_GAIN_DEN       1
#define GBC_EMU_SCALE                2
#define GBC_EMU_DISPLAY_X_OFFSET     ((BOARD_RLCD_WIDTH - GB_WIDTH * GBC_EMU_SCALE) / 2)
#define GBC_EMU_DISPLAY_Y_OFFSET     ((BOARD_RLCD_HEIGHT - GB_HEIGHT * GBC_EMU_SCALE) / 2)

static volatile uint8_t s_output_volume = GBC_EMU_AUDIO_VOLUME;

typedef struct {
    char rom_path[256];
    uint16_t *framebuffer;
    uint16_t *previous_framebuffer;
    int16_t *soundbuffer;
    bool previous_frame_valid;
    bool audio_enabled;
    uint32_t audio_clip_count;
    uint32_t audio_sample_count;
    int64_t audio_last_clip_log_us;
    volatile bool stop_requested;
    volatile bool frame_ready;
} gbc_emu_instance_t;

static gbc_emu_instance_t *s_instance;
static volatile uint8_t s_joypad_state = 0xFF;

static int gbc_emu_convert_joypad(uint8_t joypad)
{
    int pad = 0;

    if ((joypad & (1U << 0)) == 0) {
        pad |= GB_PAD_A;
    }
    if ((joypad & (1U << 1)) == 0) {
        pad |= GB_PAD_B;
    }
    if ((joypad & (1U << 2)) == 0) {
        pad |= GB_PAD_SELECT;
    }
    if ((joypad & (1U << 3)) == 0) {
        pad |= GB_PAD_START;
    }
    if ((joypad & (1U << 4)) == 0) {
        pad |= GB_PAD_RIGHT;
    }
    if ((joypad & (1U << 5)) == 0) {
        pad |= GB_PAD_LEFT;
    }
    if ((joypad & (1U << 6)) == 0) {
        pad |= GB_PAD_UP;
    }
    if ((joypad & (1U << 7)) == 0) {
        pad |= GB_PAD_DOWN;
    }

    return pad;
}

static void gbc_emu_video_callback(void *buffer)
{
    (void)buffer;
    if (s_instance != NULL) {
        s_instance->frame_ready = true;
    }
}

static void gbc_emu_audio_callback(void *buffer, size_t length)
{
    gbc_emu_instance_t *instance = s_instance;
    if (instance == NULL || !instance->audio_enabled || buffer == NULL || length == 0) {
        return;
    }

    int16_t *samples = (int16_t *)buffer;
    uint32_t clipped = 0;
    for (size_t i = 0; i < length; i++) {
        int32_t amplified = ((int32_t)samples[i] * GBC_EMU_AUDIO_GAIN_NUM) / GBC_EMU_AUDIO_GAIN_DEN;
        if (amplified > INT16_MAX) {
            amplified = INT16_MAX;
            clipped++;
        } else if (amplified < INT16_MIN) {
            amplified = INT16_MIN;
            clipped++;
        }
        samples[i] = (int16_t)amplified;
    }

    instance->audio_clip_count += clipped;
    instance->audio_sample_count += length;
    int64_t now_us = esp_timer_get_time();
    if (now_us - instance->audio_last_clip_log_us >= 1000000) {
        if (instance->audio_sample_count > 0 && instance->audio_clip_count > 0) {
            ESP_LOGW(TAG,
                     "GBC audio clipping: %u/%u samples clipped",
                     (unsigned)instance->audio_clip_count,
                     (unsigned)instance->audio_sample_count);
        }
        instance->audio_clip_count = 0;
        instance->audio_sample_count = 0;
        instance->audio_last_clip_log_us = now_us;
    }

    size_t bytes_written = 0;
    esp_err_t ret = board_speaker_write(buffer, length * sizeof(int16_t), &bytes_written, 0);
    if (ret != ESP_OK) {
        instance->audio_enabled = false;
        ESP_LOGW(TAG, "GBC audio disabled, speaker write failed: %s", esp_err_to_name(ret));
    }
}

static void gbc_emu_audio_init(gbc_emu_instance_t *instance)
{
    board_speaker_config_t speaker_config = BOARD_SPEAKER_DEFAULT_CONFIG();
    speaker_config.sample_rate_hz = GBC_EMU_AUDIO_SAMPLE_RATE;
    speaker_config.channel_count = GBC_EMU_AUDIO_CHANNELS;
    speaker_config.bits_per_sample = GBC_EMU_AUDIO_BITS;
    speaker_config.volume = s_output_volume;

    esp_err_t ret = board_speaker_init(&speaker_config);
    if (ret != ESP_OK) {
        instance->audio_enabled = false;
        ESP_LOGW(TAG, "GBC audio disabled, speaker init failed: %s", esp_err_to_name(ret));
        return;
    }

    instance->audio_enabled = true;
    ESP_LOGI(TAG,
             "GBC audio enabled: %d Hz, %d-bit stereo, volume=%d",
             GBC_EMU_AUDIO_SAMPLE_RATE,
             GBC_EMU_AUDIO_BITS,
             s_output_volume);
}

static esp_err_t gbc_emu_draw_frame(gbc_emu_instance_t *instance)
{
    bool has_dirty = false;

    for (uint16_t y = 0; y < GB_HEIGHT; y++) {
        const uint16_t *current_line = &instance->framebuffer[y * GB_WIDTH];
        uint16_t *previous_line = &instance->previous_framebuffer[y * GB_WIDTH];

        if (instance->previous_frame_valid &&
            memcmp(current_line, previous_line, GB_WIDTH * sizeof(uint16_t)) == 0) {
            continue;
        }

        uint16_t out_y = (uint16_t)(GBC_EMU_DISPLAY_Y_OFFSET + y * 2);
        ESP_RETURN_ON_ERROR(board_rlcd_draw_gbc_line_2x_rgb565_be(GBC_EMU_DISPLAY_X_OFFSET,
                                                                  out_y,
                                                                  current_line,
                                                                  GB_WIDTH),
                            TAG,
                            "draw line failed");

        memcpy(previous_line, current_line, GB_WIDTH * sizeof(uint16_t));

        has_dirty = true;
    }

    instance->previous_frame_valid = true;
    if (!has_dirty) {
        return ESP_OK;
    }

    /*
     * ST7305 的局部纵向窗口在当前驱动里还不够稳定：只 flush 若干变化行
     * 会导致画面块错位。这里保留“只重画变化行”的收益，但实际传输仍刷新
     * 完整 320x288 游戏区域，优先保证画面正确。
     */
    return board_rlcd_flush_area(GBC_EMU_DISPLAY_X_OFFSET,
                                 GBC_EMU_DISPLAY_Y_OFFSET,
                                 GBC_EMU_DISPLAY_X_OFFSET + GB_WIDTH * GBC_EMU_SCALE - 1,
                                 GBC_EMU_DISPLAY_Y_OFFSET + GB_HEIGHT * GBC_EMU_SCALE - 1);
}

static void gbc_emu_task(void *arg)
{
    gbc_emu_instance_t *instance = (gbc_emu_instance_t *)arg;
    int64_t next_frame_us = esp_timer_get_time();
    uint32_t frame_count = 0;
    uint32_t flush_count = 0;
    int64_t perf_start_us = next_frame_us;
    int64_t emu_total_us = 0;
    int64_t draw_total_us = 0;

    ESP_LOGI(TAG, "Starting gnuboy ROM: %s", instance->rom_path);
    gbc_emu_audio_init(instance);

    if (gnuboy_init(GBC_EMU_AUDIO_SAMPLE_RATE,
                    GB_AUDIO_STEREO_S16,
                    GB_PIXEL_565_BE,
                    gbc_emu_video_callback,
                    gbc_emu_audio_callback) < 0) {
        ESP_LOGE(TAG, "gnuboy init failed");
        goto exit;
    }

    gnuboy_set_framebuffer(instance->framebuffer);
    gnuboy_set_soundbuffer(instance->soundbuffer, GBC_EMU_AUDIO_BUFFER_SAMPLES);

    if (gnuboy_load_rom_file(instance->rom_path) < 0) {
        ESP_LOGE(TAG, "gnuboy load ROM failed");
        goto exit;
    }

    ESP_LOGI(TAG, "gnuboy hardware type: %s", gnuboy_get_hwtype() == GB_HW_CGB ? "GBC" : "GB");
    gnuboy_reset(true);
    ESP_ERROR_CHECK(board_rlcd_clear(BOARD_RLCD_COLOR_BLACK));
    ESP_ERROR_CHECK(board_rlcd_flush());

    while (!instance->stop_requested) {
        gnuboy_set_pad(gbc_emu_convert_joypad(s_joypad_state));

        instance->frame_ready = false;
        int64_t emu_start_us = esp_timer_get_time();
        gnuboy_run(true);
        int64_t emu_end_us = esp_timer_get_time();
        emu_total_us += emu_end_us - emu_start_us;

        if (instance->frame_ready) {
            int64_t draw_start_us = esp_timer_get_time();
            (void)gbc_emu_draw_frame(instance);
            draw_total_us += esp_timer_get_time() - draw_start_us;
            flush_count++;
        }

        frame_count++;
#if GBC_EMU_ENABLE_PERF_LOG
        if ((frame_count % 60) == 0) {
            int64_t now_us = esp_timer_get_time();
            int64_t elapsed_us = now_us - perf_start_us;
            ESP_LOGI(TAG,
                     "gnuboy perf: frames=%u flushes=%u fps=%.1f emu_avg=%lldus draw_avg=%lldus",
                     (unsigned)frame_count,
                     (unsigned)flush_count,
                     elapsed_us > 0 ? (double)frame_count * 1000000.0 / (double)elapsed_us : 0.0,
                     frame_count > 0 ? emu_total_us / frame_count : 0,
                     flush_count > 0 ? draw_total_us / flush_count : 0);
        }
#else
        (void)perf_start_us;
        (void)emu_total_us;
        (void)draw_total_us;
#endif

        next_frame_us += GBC_EMU_FRAME_US;
        int64_t now_us = esp_timer_get_time();
        int64_t delay_us = next_frame_us - now_us;
        if (delay_us > 1000) {
            vTaskDelay(pdMS_TO_TICKS((uint32_t)(delay_us / 1000)));
        } else {
            /*
             * 落后一帧时不能每帧 vTaskDelay(1)，否则一个 FreeRTOS tick
             * 会把帧率直接拖到 30 多 FPS。这里大部分时间只 yield，
             * 每 32 帧真正 delay 一次，让 CPU1 idle task 有机会运行。
             */
            next_frame_us = now_us;
            if ((frame_count & 0x1FU) == 0) {
                vTaskDelay(1);
            } else {
                taskYIELD();
            }
        }
    }

exit:
    gnuboy_free_rom();
    heap_caps_free(instance->framebuffer);
    heap_caps_free(instance->previous_framebuffer);
    heap_caps_free(instance->soundbuffer);
    s_instance = NULL;
    heap_caps_free(instance);
    vTaskDelete(NULL);
}

void gbc_emu_set_joypad(uint8_t joypad)
{
    s_joypad_state = joypad;
}

void gbc_emu_set_volume(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }

    s_output_volume = volume;
    if (board_speaker_set_volume(volume) != ESP_OK) {
        ESP_LOGW(TAG, "set GBC speaker volume failed");
    }
}

uint8_t gbc_emu_get_volume(void)
{
    return s_output_volume;
}

esp_err_t gbc_emu_start_file(const char *rom_path)
{
    if (rom_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_instance != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!board_rlcd_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    gbc_emu_instance_t *instance = heap_caps_calloc(1, sizeof(gbc_emu_instance_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (instance == NULL) {
        return ESP_ERR_NO_MEM;
    }

    strlcpy(instance->rom_path, rom_path, sizeof(instance->rom_path));
    s_joypad_state = 0xFF;
    instance->framebuffer = heap_caps_malloc(GB_WIDTH * GB_HEIGHT * sizeof(uint16_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    instance->previous_framebuffer = heap_caps_malloc(GB_WIDTH * GB_HEIGHT * sizeof(uint16_t),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    instance->soundbuffer = heap_caps_malloc(GBC_EMU_AUDIO_BUFFER_SAMPLES * 2 * sizeof(int16_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (instance->framebuffer == NULL || instance->previous_framebuffer == NULL || instance->soundbuffer == NULL) {
        heap_caps_free(instance->framebuffer);
        heap_caps_free(instance->previous_framebuffer);
        heap_caps_free(instance->soundbuffer);
        heap_caps_free(instance);
        return ESP_ERR_NO_MEM;
    }

    s_instance = instance;

    BaseType_t task_ret = xTaskCreatePinnedToCore(gbc_emu_task,
                                                  "gbc_emu",
                                                  GBC_EMU_TASK_STACK_SIZE,
                                                  instance,
                                                  GBC_EMU_TASK_PRIORITY,
                                                  NULL,
                                                  GBC_EMU_TASK_CORE);
    if (task_ret != pdPASS) {
        s_instance = NULL;
        heap_caps_free(instance->framebuffer);
        heap_caps_free(instance->previous_framebuffer);
        heap_caps_free(instance->soundbuffer);
        heap_caps_free(instance);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t gbc_emu_stop(void)
{
    gbc_emu_instance_t *instance = s_instance;
    if (instance == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    instance->stop_requested = true;
    for (uint16_t i = 0; i < 100; i++) {
        if (s_instance == NULL) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_ERR_TIMEOUT;
}
