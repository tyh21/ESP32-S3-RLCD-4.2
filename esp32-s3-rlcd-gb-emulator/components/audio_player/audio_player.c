#include "audio_player.h"
#include "board_speaker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD  /* ESP32-S3 XTensa: no SSE/NEON, use generic path */
#include "minimp3.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_SIMD  /* ESP32-S3 XTensa: no SSE/NEON */
#include "dr_flac.h"

static const char *TAG = "audio_player";

#define AUDIO_PLAYER_TASK_STACK   (16 * 1024)
#define AUDIO_PLAYER_TASK_PRIO     3
#define AUDIO_READ_BUF_SIZE        (16 * 1024)   /* 16KB read buffer for MP3 */
#define AUDIO_PCM_BUF_SIZE         (MINIMP3_MAX_SAMPLES_PER_FRAME * 2)  /* max one MP3 frame output */
#define AUDIO_FLAC_PCM_FRAMES      8192   /* frames per FLAC read call */
#define AUDIO_DEFAULT_VOLUME       70

/* ── WAV header parsing ───────────────────────────────────────────── */

#pragma pack(push, 1)
typedef struct {
    char     riff[4];        /* "RIFF" */
    uint32_t file_size;
    char     wave[4];        /* "WAVE" */
    char     fmt[4];         /* "fmt " */
    uint32_t fmt_size;
    uint16_t audio_format;   /* 1 = PCM */
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_header_t;
#pragma pack(pop)

/* ── State ────────────────────────────────────────────────────────── */

static audio_player_state_t  s_state   = AUDIO_PLAYER_STATE_IDLE;
static audio_player_format_t s_format  = AUDIO_PLAYER_FORMAT_UNKNOWN;
static char     s_path[256]  = {0};
static uint32_t s_sample_rate = 0;
static uint8_t  s_channels    = 0;
static uint8_t  s_volume      = AUDIO_DEFAULT_VOLUME;
static TaskHandle_t s_task    = NULL;
static bool s_stop_requested  = false;
static bool s_pause_requested = false;

/* ── Helpers ──────────────────────────────────────────────────────── */

static bool ap_has_ext(const char *path, const char *ext)
{
    /* ext includes the leading dot, e.g. ".wav" or ".mp3" */
    const char *dot = strrchr(path, '.');
    if (dot == NULL) return false;
    size_t ext_len = strlen(ext);
    if (strlen(dot) != ext_len) return false;
    for (size_t i = 0; i < ext_len; i++) {
        if (tolower((unsigned char)dot[i]) != tolower((unsigned char)ext[i]))
            return false;
    }
    return true;
}

bool audio_player_is_supported_file(const char *path)
{
    return ap_has_ext(path, ".wav") || ap_has_ext(path, ".mp3") || ap_has_ext(path, ".flac");
}

audio_player_state_t audio_player_get_state(void)  { return s_state; }
audio_player_format_t audio_player_get_format(void) { return s_format; }
const char *audio_player_get_path(void)             { return s_path; }
uint32_t audio_player_get_sample_rate(void)         { return s_sample_rate; }

esp_err_t audio_player_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_volume = volume;
    return board_speaker_set_volume(volume);
}

uint8_t audio_player_get_volume(void)
{
    return s_volume;
}

/* ── WAV player ──────────────────────────────────────────────────── */

static esp_err_t ap_play_wav(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "WAV: cannot open %s", path);
        return ESP_FAIL;
    }

    wav_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        ESP_LOGE(TAG, "WAV: header read failed");
        fclose(f);
        return ESP_FAIL;
    }

    if (memcmp(hdr.riff, "RIFF", 4) != 0 || memcmp(hdr.wave, "WAVE", 4) != 0 ||
        memcmp(hdr.fmt,  "fmt ", 4) != 0 || hdr.audio_format != 1) {
        ESP_LOGE(TAG, "WAV: unsupported format (not PCM)");
        fclose(f);
        return ESP_FAIL;
    }

    if (hdr.bits_per_sample != 16) {
        ESP_LOGE(TAG, "WAV: only 16-bit supported, got %u", hdr.bits_per_sample);
        fclose(f);
        return ESP_FAIL;
    }

    if (hdr.channels < 1 || hdr.channels > 2) {
        ESP_LOGE(TAG, "WAV: only mono/stereo supported, got %u", hdr.channels);
        fclose(f);
        return ESP_FAIL;
    }

    /* Skip extra fmt bytes and any non-data chunks to find "data" chunk */
    if (hdr.fmt_size > 16) {
        fseek(f, (long)(hdr.fmt_size - 16), SEEK_CUR);
    }
    char chunk_id[4];
    uint32_t chunk_size = 0;
    /* scan for "data" chunk */
    bool found_data = false;
    while (fread(chunk_id, 1, 4, f) == 4) {
        if (fread(&chunk_size, 4, 1, f) != 1) break;
        if (memcmp(chunk_id, "data", 4) == 0) {
            found_data = true;
            break;
        }
        fseek(f, (long)chunk_size, SEEK_CUR);
    }
    if (!found_data) {
        ESP_LOGE(TAG, "WAV: data chunk not found");
        fclose(f);
        return ESP_FAIL;
    }

    s_sample_rate = hdr.sample_rate;
    s_channels    = (uint8_t)hdr.channels;

    ESP_LOGI(TAG, "WAV: %u Hz, %u ch, %u-bit, data=%u bytes",
             s_sample_rate, s_channels, hdr.bits_per_sample, chunk_size);

    /* Initialize speaker - always stereo for I2S TDM */
    board_speaker_config_t spk_cfg = {
        .sample_rate_hz  = s_sample_rate,
        .channel_count   = 2,  /* always stereo for I2S TDM */
        .bits_per_sample = 16,
        .volume          = s_volume,
    };
    esp_err_t ret = board_speaker_init(&spk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WAV: speaker init failed: %s", esp_err_to_name(ret));
        fclose(f);
        return ret;
    }

    /*
     * Playback loop.
     * For mono WAV: read raw mono samples, expand to stereo in a separate
     * output buffer that is twice the size, then write to speaker.
     * For stereo WAV: read directly into the buffer and write as-is.
     */
    const size_t raw_buf_size = 4096;   /* raw read from file */
    const size_t out_buf_size = raw_buf_size * 2;  /* stereo expansion may double */
    int16_t *pcm = heap_caps_malloc(out_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc(out_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (pcm == NULL) {
        ESP_LOGE(TAG, "WAV: pcm buffer alloc failed");
        board_speaker_deinit();
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    /* Use the second half of the buffer for mono->stereo expansion output */
    int16_t *out_pcm = (int16_t *)((uint8_t *)pcm + raw_buf_size);

    s_state = AUDIO_PLAYER_STATE_PLAYING;
    size_t total_read = 0;
    while (!s_stop_requested && total_read < chunk_size) {
        /* Handle pause */
        if (s_pause_requested) {
            s_state = AUDIO_PLAYER_STATE_PAUSED;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        s_state = AUDIO_PLAYER_STATE_PLAYING;

        /* Read raw PCM data from file */
        size_t to_read = raw_buf_size;
        if (total_read + to_read > chunk_size) {
            to_read = chunk_size - total_read;
        }
        /* For mono, read an even number of bytes (whole 16-bit samples) */
        if (s_channels == 1 && (to_read & 1)) {
            to_read--;
        }
        size_t got = fread(pcm, 1, to_read, f);
        if (got == 0) break;
        total_read += got;

        size_t write_bytes;
        const int16_t *write_buf;

        if (s_channels == 1) {
            /* Expand mono to stereo: each sample duplicated to L and R */
            size_t samples = got / 2;  /* 16-bit samples */
            for (size_t i = 0; i < samples; i++) {
                out_pcm[i * 2]     = pcm[i];
                out_pcm[i * 2 + 1] = pcm[i];
            }
            write_bytes = samples * 4;  /* stereo: 2x int16_t per sample */
            write_buf = out_pcm;
        } else {
            write_bytes = got;
            write_buf = pcm;
        }

        size_t written = 0;
        ret = board_speaker_write(write_buf, write_bytes, &written, 1000);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WAV: write failed: %s", esp_err_to_name(ret));
            break;
        }
    }

    free(pcm);
    board_speaker_deinit();
    fclose(f);
    ESP_LOGI(TAG, "WAV: playback done (%u bytes)", (unsigned)total_read);
    return ESP_OK;
}

/* ── MP3 player ──────────────────────────────────────────────────── */

static esp_err_t ap_play_mp3(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "MP3: cannot open %s", path);
        return ESP_FAIL;
    }

    mp3dec_t mp3dec;
    mp3dec_init(&mp3dec);

    uint8_t *read_buf = heap_caps_malloc(AUDIO_READ_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (read_buf == NULL) {
        read_buf = heap_caps_malloc(AUDIO_READ_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    int16_t *pcm_buf = heap_caps_malloc(AUDIO_PCM_BUF_SIZE * sizeof(int16_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm_buf == NULL) {
        pcm_buf = heap_caps_malloc(AUDIO_PCM_BUF_SIZE * sizeof(int16_t),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (read_buf == NULL || pcm_buf == NULL) {
        ESP_LOGE(TAG, "MP3: buffer alloc failed");
        free(read_buf); free(pcm_buf);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    /* Fill initial buffer */
    size_t buf_fill = fread(read_buf, 1, AUDIO_READ_BUF_SIZE, f);
    if (buf_fill == 0) {
        ESP_LOGE(TAG, "MP3: empty file");
        free(read_buf); free(pcm_buf);
        fclose(f);
        return ESP_FAIL;
    }

    /* Decode first frame to get sample rate & channels */
    mp3dec_frame_info_t info;
    int decoded_samples = 0;
    size_t buf_consume = 0;

    /* Try to find first valid frame */
    size_t offset = 0;
    while (offset < buf_fill) {
        decoded_samples = mp3dec_decode_frame(&mp3dec, read_buf + offset,
                                               (int)(buf_fill - offset),
                                               pcm_buf, &info);
        if (decoded_samples > 0 && info.frame_bytes > 0) {
            buf_consume = offset + info.frame_bytes;
            break;
        }
        offset++;
    }

    if (decoded_samples == 0) {
        ESP_LOGE(TAG, "MP3: no valid frame found");
        free(read_buf); free(pcm_buf);
        fclose(f);
        return ESP_FAIL;
    }

    s_sample_rate = (uint32_t)info.hz;
    s_channels    = (uint8_t)info.channels;
    ESP_LOGI(TAG, "MP3: %u Hz, %u ch, layer %d, bitrate %d kbps",
             s_sample_rate, s_channels, info.layer, info.bitrate_kbps);

    /* Initialize speaker with first frame's sample rate */
    board_speaker_config_t spk_cfg = {
        .sample_rate_hz  = s_sample_rate,
        .channel_count   = 2,  /* always stereo for I2S TDM */
        .bits_per_sample = 16,
        .volume          = s_volume,
    };
    esp_err_t ret = board_speaker_init(&spk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MP3: speaker init failed: %s", esp_err_to_name(ret));
        free(read_buf); free(pcm_buf);
        fclose(f);
        return ret;
    }

    /* Write first decoded frame */
    s_state = AUDIO_PLAYER_STATE_PLAYING;

    /* Playback loop */
    size_t consumed = buf_consume;
    while (!s_stop_requested) {
        /* Handle pause */
        if (s_pause_requested) {
            s_state = AUDIO_PLAYER_STATE_PAUSED;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        s_state = AUDIO_PLAYER_STATE_PLAYING;

        /* If we consumed most of the buffer, shift remaining and refill */
        if (consumed > AUDIO_READ_BUF_SIZE / 2) {
            size_t remaining = buf_fill - consumed;
            memmove(read_buf, read_buf + consumed, remaining);
            size_t to_read = AUDIO_READ_BUF_SIZE - remaining;
            size_t got = fread(read_buf + remaining, 1, to_read, f);
            buf_fill = remaining + got;
            consumed = 0;

            if (got == 0 && remaining == 0) break;  /* EOF */
            if (got == 0 && remaining < 4) break;    /* not enough for a frame */
        }

        /* Decode next frame */
        decoded_samples = mp3dec_decode_frame(&mp3dec,
                                              read_buf + consumed,
                                              (int)(buf_fill - consumed),
                                              pcm_buf, &info);
        if (decoded_samples > 0 && info.frame_bytes > 0) {
            /* If sample rate changed mid-stream (rare), just keep going */
            size_t write_bytes = (size_t)decoded_samples * sizeof(int16_t);

            /* If mono, duplicate to stereo */
            if (info.channels == 1) {
                for (int i = decoded_samples - 1; i >= 0; i--) {
                    pcm_buf[i * 2]     = pcm_buf[i];
                    pcm_buf[i * 2 + 1] = pcm_buf[i];
                }
                write_bytes = (size_t)decoded_samples * 2 * sizeof(int16_t);
            }

            size_t written = 0;
            ret = board_speaker_write(pcm_buf, write_bytes, &written, 2000);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "MP3: write failed: %s", esp_err_to_name(ret));
                break;
            }
            consumed += info.frame_bytes;
        } else {
            /* Frame sync lost, skip one byte */
            consumed++;
            if (consumed >= buf_fill) {
                /* Try to refill */
                size_t to_read = AUDIO_READ_BUF_SIZE;
                size_t got = fread(read_buf, 1, to_read, f);
                buf_fill = got;
                consumed = 0;
                if (got == 0) break;  /* EOF */
            }
        }
    }

    free(read_buf);
    free(pcm_buf);
    board_speaker_deinit();
    fclose(f);
    ESP_LOGI(TAG, "MP3: playback done");
    return ESP_OK;
}

/* ── FLAC player ─────────────────────────────────────────────────── */

static esp_err_t ap_play_flac(const char *path)
{
    drflac *flac = drflac_open_file(path, NULL);
    if (flac == NULL) {
        ESP_LOGE(TAG, "FLAC: cannot open %s", path);
        return ESP_FAIL;
    }

    s_sample_rate = (uint32_t)flac->sampleRate;
    s_channels    = (uint8_t)flac->channels;
    ESP_LOGI(TAG, "FLAC: %u Hz, %u ch, %llu frames",
             s_sample_rate, s_channels,
             (unsigned long long)flac->totalPCMFrameCount);

    if (s_channels < 1 || s_channels > 2) {
        ESP_LOGE(TAG, "FLAC: only mono/stereo supported, got %u", s_channels);
        drflac_close(flac);
        return ESP_FAIL;
    }

    /* Initialize speaker - always stereo for I2S TDM */
    board_speaker_config_t spk_cfg = {
        .sample_rate_hz  = s_sample_rate,
        .channel_count   = 2,
        .bits_per_sample = 16,
        .volume          = s_volume,
    };
    esp_err_t ret = board_speaker_init(&spk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FLAC: speaker init failed: %s", esp_err_to_name(ret));
        drflac_close(flac);
        return ret;
    }

    /* Allocate PCM buffer in internal SRAM (higher bandwidth than PSRAM) */
    size_t buf_size = AUDIO_FLAC_PCM_FRAMES * 2 * sizeof(int16_t);
    int16_t *pcm = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (pcm == NULL) {
        ESP_LOGE(TAG, "FLAC: pcm buffer alloc failed (%u bytes)", (unsigned)buf_size);
        board_speaker_deinit();
        drflac_close(flac);
        return ESP_ERR_NO_MEM;
    }

    s_state = AUDIO_PLAYER_STATE_PLAYING;
    while (!s_stop_requested) {
        if (s_pause_requested) {
            s_state = AUDIO_PLAYER_STATE_PAUSED;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        s_state = AUDIO_PLAYER_STATE_PLAYING;

        /* Decode PCM frames into buffer */
        drflac_uint64 frames = drflac_read_pcm_frames_s16(flac, AUDIO_FLAC_PCM_FRAMES, pcm);
        if (frames == 0) break;  /* EOF */

        /* Expand mono → stereo in-place (work backwards to avoid overwrite) */
        size_t write_bytes;
        if (s_channels == 1) {
            for (drflac_uint64 i = frames; i > 0; i--) {
                pcm[(i - 1) * 2]     = pcm[i - 1];
                pcm[(i - 1) * 2 + 1] = pcm[i - 1];
            }
            write_bytes = (size_t)frames * 2 * sizeof(int16_t);
        } else {
            write_bytes = (size_t)frames * 2 * sizeof(int16_t);
        }

        size_t written = 0;
        ret = board_speaker_write(pcm, write_bytes, &written, 2000);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "FLAC: write failed: %s", esp_err_to_name(ret));
            break;
        }
    }

    free(pcm);
    board_speaker_deinit();
    drflac_close(flac);
    ESP_LOGI(TAG, "FLAC: playback done");
    return ESP_OK;
}

/* ── Playback task ─────────────────────────────────────────────────── */

static void ap_playback_task(void *arg)
{
    const char *path = (const char *)arg;

    ESP_LOGI(TAG, "Playback task started: %s", path);

    esp_err_t ret;
    if (ap_has_ext(path, ".wav")) {
        s_format = AUDIO_PLAYER_FORMAT_WAV;
        ret = ap_play_wav(path);
    } else if (ap_has_ext(path, ".mp3")) {
        s_format = AUDIO_PLAYER_FORMAT_MP3;
        ret = ap_play_mp3(path);
    } else if (ap_has_ext(path, ".flac")) {
        s_format = AUDIO_PLAYER_FORMAT_FLAC;
        ret = ap_play_flac(path);
    } else {
        ESP_LOGE(TAG, "Unsupported file: %s", path);
        s_state = AUDIO_PLAYER_STATE_ERROR;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (ret != ESP_OK) {
        s_state = AUDIO_PLAYER_STATE_ERROR;
    } else if (!s_stop_requested) {
        s_state = AUDIO_PLAYER_STATE_IDLE;  /* finished naturally */
    } else {
        s_state = AUDIO_PLAYER_STATE_STOPPED;
    }

    s_stop_requested = false;
    s_pause_requested = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t audio_player_play(const char *path)
{
    if (path == NULL) return ESP_ERR_INVALID_ARG;
    if (s_task != NULL) {
        ESP_LOGW(TAG, "Already playing, stop first");
        audio_player_stop();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    strlcpy(s_path, path, sizeof(s_path));
    s_stop_requested = false;
    s_pause_requested = false;
    s_state = AUDIO_PLAYER_STATE_STARTING;

    BaseType_t xret = xTaskCreate(ap_playback_task, "audio_player",
                                  AUDIO_PLAYER_TASK_STACK, s_path,
                                  AUDIO_PLAYER_TASK_PRIO, &s_task);
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create playback task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_player_stop(void)
{
    if (s_task == NULL) return ESP_OK;
    s_stop_requested = true;
    /* Wait for task to finish */
    for (int i = 0; i < 50 && s_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (s_task != NULL) {
        /* Force delete if stuck */
        vTaskDelete(s_task);
        s_task = NULL;
    }
    s_state = AUDIO_PLAYER_STATE_STOPPED;
    return ESP_OK;
}

esp_err_t audio_player_pause(void)
{
    if (s_task == NULL) return ESP_ERR_INVALID_STATE;
    s_pause_requested = true;
    return ESP_OK;
}

esp_err_t audio_player_resume(void)
{
    s_pause_requested = false;
    return ESP_OK;
}
