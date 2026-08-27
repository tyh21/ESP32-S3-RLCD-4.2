#include "gb_emu.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_rlcd.h"
#include "board_speaker.h"

/*
 * Peanut-GB 默认偏向显示准确性和 12 色调色板信息。
 * 当前屏幕是 1bit 黑白 RLCD，最终只需要判断像素亮/灭，
 * 所以关闭这些额外显示路径，减少每帧 LCD 线渲染开销。
 */
#define PEANUT_GB_12_COLOUR            0
#define PEANUT_GB_HIGH_LCD_ACCURACY    0
#define ENABLE_SOUND                    1
static uint8_t audio_read(uint_fast16_t addr);
static void audio_write(uint_fast16_t addr, uint8_t val);
#include "peanut_gb.h"

static const char *TAG = "gb_emu";

#ifndef GB_EMU_ENABLE_PERF_LOG
#define GB_EMU_ENABLE_PERF_LOG 0
#endif

#define GB_EMU_SCREEN_WIDTH             160
#define GB_EMU_SCREEN_HEIGHT            144
#define GB_EMU_DISPLAY_SCALE            2
#define GB_EMU_DISPLAY_X_OFFSET         ((BOARD_RLCD_WIDTH - GB_EMU_SCREEN_WIDTH * GB_EMU_DISPLAY_SCALE) / 2)
#define GB_EMU_DISPLAY_Y_OFFSET         ((BOARD_RLCD_HEIGHT - GB_EMU_SCREEN_HEIGHT * GB_EMU_DISPLAY_SCALE) / 2)
#define GB_EMU_TASK_STACK_SIZE          (12 * 1024)
#define GB_EMU_TASK_PRIORITY            4
#define GB_EMU_TASK_CORE                1
#define GB_EMU_IDLE_DELAY_INTERVAL      30
#define GB_EMU_FRAME_US                 16742

#define GB_EMU_AUDIO_SAMPLE_RATE        24000
#define GB_EMU_AUDIO_CHANNELS           2
#define GB_EMU_AUDIO_BITS               16
#define GB_EMU_AUDIO_VOLUME             80
#define GB_EMU_AUDIO_MAX_SAMPLES        410
#define GB_EMU_AUDIO_MASTER_GAIN        6000
#define GB_EMU_AUDIO_STARTUP_TEST       0

static volatile uint8_t s_output_volume = GB_EMU_AUDIO_VOLUME;

#define GB_HEADER_TITLE_START          0x0134
#define GB_HEADER_TITLE_END            0x0143
#define GB_HEADER_CGB_FLAG             0x0143
#define GB_HEADER_SGB_FLAG             0x0146
#define GB_HEADER_CARTRIDGE_TYPE       0x0147
#define GB_HEADER_ROM_SIZE             0x0148
#define GB_HEADER_RAM_SIZE             0x0149
#define GB_HEADER_DESTINATION_CODE     0x014A
#define GB_HEADER_MASK_ROM_VERSION     0x014C
#define GB_HEADER_HEADER_CHECKSUM      0x014D
#define GB_MIN_ROM_SIZE                0x0150

typedef struct {
    struct gb_s gb;
    const gb_emu_rom_t *rom;
    uint8_t *cart_ram;
    size_t cart_ram_size;
    uint32_t frame_count;
    uint32_t flush_count;
    uint32_t audio_sample_remainder;
    bool frame_dirty;
    bool audio_enabled;
    volatile bool stop_requested;
} gb_emu_instance_t;

typedef struct {
    uint8_t regs[0x30];
    bool square_enabled[2];
    uint16_t square_freq_raw[2];
    uint8_t square_volume[2];
    uint8_t square_duty[2];
    uint8_t square_envelope_period[2];
    uint8_t square_envelope_timer[2];
    uint16_t square_length[2];
    uint32_t square_phase[2];
    bool square_envelope_increase[2];
    bool wave_enabled;
    uint16_t wave_freq_raw;
    uint16_t wave_length;
    uint32_t wave_phase;
    bool noise_enabled;
    uint8_t noise_volume;
    uint8_t noise_envelope_period;
    uint8_t noise_envelope_timer;
    uint16_t noise_lfsr;
    uint16_t noise_length;
    uint32_t noise_phase;
    uint32_t frame_sequencer_remainder;
    uint8_t frame_sequencer_step;
    bool noise_envelope_increase;
} gb_emu_apu_t;

static gb_emu_instance_t *s_instance = NULL;
static gb_emu_apu_t s_apu;

static uint8_t audio_read(uint_fast16_t addr)
{
    if (addr < 0xFF10 || addr > 0xFF3F) {
        return 0xFF;
    }

    if (addr == 0xFF26) {
        uint8_t status = s_apu.regs[addr - 0xFF10] | 0x70;
        if (s_apu.square_enabled[0]) {
            status |= 0x01;
        }
        if (s_apu.square_enabled[1]) {
            status |= 0x02;
        }
        if (s_apu.wave_enabled) {
            status |= 0x04;
        }
        if (s_apu.noise_enabled) {
            status |= 0x08;
        }
        return status;
    }

    return s_apu.regs[addr - 0xFF10];
}

static uint8_t gb_emu_apu_initial_volume(uint8_t envelope_reg)
{
    return (envelope_reg >> 4) & 0x0F;
}

static bool gb_emu_apu_dac_enabled(uint8_t envelope_reg)
{
    return (envelope_reg & 0xF8) != 0;
}

static void gb_emu_apu_trigger_square(uint8_t channel)
{
    uint16_t reg_base = channel == 0 ? 0 : 5;
    uint8_t envelope = s_apu.regs[reg_base + 2];
    uint8_t length_load = s_apu.regs[reg_base + 1] & 0x3F;

    s_apu.square_enabled[channel] = gb_emu_apu_dac_enabled(envelope);
    s_apu.square_volume[channel] = gb_emu_apu_initial_volume(envelope);
    s_apu.square_duty[channel] = (s_apu.regs[reg_base + 1] >> 6) & 0x03;
    s_apu.square_envelope_period[channel] = envelope & 0x07;
    s_apu.square_envelope_timer[channel] = s_apu.square_envelope_period[channel] ? s_apu.square_envelope_period[channel] : 8;
    s_apu.square_envelope_increase[channel] = (envelope & 0x08) != 0;
    s_apu.square_length[channel] = 64 - length_load;
    s_apu.square_phase[channel] = 0;
    s_apu.square_freq_raw[channel] = (uint16_t)s_apu.regs[reg_base + 3] |
                                     (uint16_t)((s_apu.regs[reg_base + 4] & 0x07) << 8);
}

static void gb_emu_apu_trigger_wave(void)
{
    s_apu.wave_enabled = (s_apu.regs[0x1A - 0x10] & 0x80) != 0;
    s_apu.wave_length = 256 - s_apu.regs[0x1B - 0x10];
    s_apu.wave_phase = 0;
    s_apu.wave_freq_raw = (uint16_t)s_apu.regs[0x1D - 0x10] |
                          (uint16_t)((s_apu.regs[0x1E - 0x10] & 0x07) << 8);
}

static void gb_emu_apu_trigger_noise(void)
{
    uint8_t envelope = s_apu.regs[0x21 - 0x10];

    s_apu.noise_enabled = gb_emu_apu_dac_enabled(envelope);
    s_apu.noise_volume = gb_emu_apu_initial_volume(envelope);
    s_apu.noise_envelope_period = envelope & 0x07;
    s_apu.noise_envelope_timer = s_apu.noise_envelope_period ? s_apu.noise_envelope_period : 8;
    s_apu.noise_envelope_increase = (envelope & 0x08) != 0;
    s_apu.noise_length = 64 - (s_apu.regs[0x20 - 0x10] & 0x3F);
    s_apu.noise_lfsr = 0x7FFF;
    s_apu.noise_phase = 0;
}

static bool gb_emu_apu_square_length_enabled(uint8_t channel)
{
    uint16_t reg_base = channel == 0 ? 0 : 5;
    return (s_apu.regs[reg_base + 4] & 0x40) != 0;
}

static bool gb_emu_apu_wave_length_enabled(void)
{
    return (s_apu.regs[0x1E - 0x10] & 0x40) != 0;
}

static bool gb_emu_apu_noise_length_enabled(void)
{
    return (s_apu.regs[0x23 - 0x10] & 0x40) != 0;
}

static void gb_emu_apu_clock_length(void)
{
    for (uint8_t channel = 0; channel < 2; channel++) {
        if (s_apu.square_enabled[channel] &&
            gb_emu_apu_square_length_enabled(channel) &&
            s_apu.square_length[channel] > 0) {
            s_apu.square_length[channel]--;
            if (s_apu.square_length[channel] == 0) {
                s_apu.square_enabled[channel] = false;
            }
        }
    }

    if (s_apu.wave_enabled && gb_emu_apu_wave_length_enabled() && s_apu.wave_length > 0) {
        s_apu.wave_length--;
        if (s_apu.wave_length == 0) {
            s_apu.wave_enabled = false;
        }
    }

    if (s_apu.noise_enabled && gb_emu_apu_noise_length_enabled() && s_apu.noise_length > 0) {
        s_apu.noise_length--;
        if (s_apu.noise_length == 0) {
            s_apu.noise_enabled = false;
        }
    }
}

static uint8_t gb_emu_apu_clock_envelope_volume(uint8_t volume,
                                                bool increase,
                                                uint8_t period,
                                                uint8_t *timer)
{
    if (period == 0) {
        return volume;
    }

    if (*timer > 0) {
        (*timer)--;
    }

    if (*timer == 0) {
        *timer = period;
        if (increase) {
            if (volume < 15) {
                volume++;
            }
        } else if (volume > 0) {
            volume--;
        }
    }

    return volume;
}

static void gb_emu_apu_clock_envelope(void)
{
    for (uint8_t channel = 0; channel < 2; channel++) {
        s_apu.square_volume[channel] = gb_emu_apu_clock_envelope_volume(
            s_apu.square_volume[channel],
            s_apu.square_envelope_increase[channel],
            s_apu.square_envelope_period[channel],
            &s_apu.square_envelope_timer[channel]);
    }

    s_apu.noise_volume = gb_emu_apu_clock_envelope_volume(s_apu.noise_volume,
                                                          s_apu.noise_envelope_increase,
                                                          s_apu.noise_envelope_period,
                                                          &s_apu.noise_envelope_timer);
}

static void gb_emu_apu_clock_frame_sequencer(void)
{
    s_apu.frame_sequencer_remainder += 512;
    if (s_apu.frame_sequencer_remainder < GB_EMU_AUDIO_SAMPLE_RATE) {
        return;
    }

    s_apu.frame_sequencer_remainder -= GB_EMU_AUDIO_SAMPLE_RATE;

    if ((s_apu.frame_sequencer_step & 1) == 0) {
        gb_emu_apu_clock_length();
    }
    if (s_apu.frame_sequencer_step == 7) {
        gb_emu_apu_clock_envelope();
    }

    s_apu.frame_sequencer_step = (uint8_t)((s_apu.frame_sequencer_step + 1) & 0x07);
}

static void audio_write(uint_fast16_t addr, uint8_t val)
{
    if (addr < 0xFF10 || addr > 0xFF3F) {
        return;
    }

    uint8_t reg_index = (uint8_t)(addr - 0xFF10);
    s_apu.regs[reg_index] = val;

    if (addr == 0xFF26) {
        if ((val & 0x80) == 0) {
            memset(&s_apu, 0, sizeof(s_apu));
        }
        return;
    }

    if ((s_apu.regs[0x26 - 0x10] & 0x80) == 0) {
        return;
    }

    switch (addr) {
    case 0xFF11:
        s_apu.square_duty[0] = (val >> 6) & 0x03;
        break;
    case 0xFF12:
        if (!gb_emu_apu_dac_enabled(val)) {
            s_apu.square_enabled[0] = false;
        }
        break;
    case 0xFF13:
        s_apu.square_freq_raw[0] = (uint16_t)val | (uint16_t)((s_apu.regs[0x14 - 0x10] & 0x07) << 8);
        break;
    case 0xFF14:
        s_apu.square_freq_raw[0] = (uint16_t)s_apu.regs[0x13 - 0x10] | (uint16_t)((val & 0x07) << 8);
        if (val & 0x80) {
            gb_emu_apu_trigger_square(0);
        }
        break;
    case 0xFF16:
        s_apu.square_duty[1] = (val >> 6) & 0x03;
        break;
    case 0xFF17:
        if (!gb_emu_apu_dac_enabled(val)) {
            s_apu.square_enabled[1] = false;
        }
        break;
    case 0xFF18:
        s_apu.square_freq_raw[1] = (uint16_t)val | (uint16_t)((s_apu.regs[0x19 - 0x10] & 0x07) << 8);
        break;
    case 0xFF19:
        s_apu.square_freq_raw[1] = (uint16_t)s_apu.regs[0x18 - 0x10] | (uint16_t)((val & 0x07) << 8);
        if (val & 0x80) {
            gb_emu_apu_trigger_square(1);
        }
        break;
    case 0xFF1A:
        if ((val & 0x80) == 0) {
            s_apu.wave_enabled = false;
        }
        break;
    case 0xFF1D:
        s_apu.wave_freq_raw = (uint16_t)val | (uint16_t)((s_apu.regs[0x1E - 0x10] & 0x07) << 8);
        break;
    case 0xFF1E:
        s_apu.wave_freq_raw = (uint16_t)s_apu.regs[0x1D - 0x10] | (uint16_t)((val & 0x07) << 8);
        if (val & 0x80) {
            gb_emu_apu_trigger_wave();
        }
        break;
    case 0xFF21:
        if (!gb_emu_apu_dac_enabled(val)) {
            s_apu.noise_enabled = false;
        }
        break;
    case 0xFF23:
        if (val & 0x80) {
            gb_emu_apu_trigger_noise();
        }
        break;
    default:
        break;
    }
}

static int16_t gb_emu_apu_mix_square(uint8_t channel)
{
    if (!s_apu.square_enabled[channel] || s_apu.square_volume[channel] == 0) {
        return 0;
    }

    uint16_t freq_raw = s_apu.square_freq_raw[channel];
    if (freq_raw >= 2048) {
        return 0;
    }

    uint32_t frequency_hz = 131072U / (uint32_t)(2048U - freq_raw);
    if (frequency_hz == 0) {
        return 0;
    }

    uint32_t step = (uint32_t)(((uint64_t)frequency_hz << 16) / GB_EMU_AUDIO_SAMPLE_RATE);
    s_apu.square_phase[channel] += step;
    uint8_t phase = (uint8_t)((s_apu.square_phase[channel] >> 13) & 0x07);
    static const uint8_t duty_threshold[4] = {1, 2, 4, 6};
    int32_t sample = phase < duty_threshold[s_apu.square_duty[channel]] ? 1 : -1;
    return (int16_t)(sample * (int32_t)s_apu.square_volume[channel] * GB_EMU_AUDIO_MASTER_GAIN / 15);
}

static int16_t gb_emu_apu_mix_wave(void)
{
    if (!s_apu.wave_enabled || s_apu.wave_freq_raw >= 2048) {
        return 0;
    }

    uint8_t volume_code = (s_apu.regs[0x1C - 0x10] >> 5) & 0x03;
    if (volume_code == 0) {
        return 0;
    }

    uint32_t frequency_hz = 65536U / (uint32_t)(2048U - s_apu.wave_freq_raw);
    uint32_t step = (uint32_t)(((uint64_t)frequency_hz << 16) / GB_EMU_AUDIO_SAMPLE_RATE);
    s_apu.wave_phase += step;

    uint8_t wave_index = (uint8_t)((s_apu.wave_phase >> 11) & 0x1F);
    uint8_t wave_byte = s_apu.regs[0x20 + (wave_index >> 1)];
    uint8_t sample4 = (wave_index & 1) ? (wave_byte & 0x0F) : (wave_byte >> 4);
    int32_t centered = (int32_t)sample4 - 8;

    if (volume_code == 2) {
        centered >>= 1;
    } else if (volume_code == 3) {
        centered >>= 2;
    }

    return (int16_t)(centered * GB_EMU_AUDIO_MASTER_GAIN / 8);
}

static int16_t gb_emu_apu_mix_noise(void)
{
    if (!s_apu.noise_enabled) {
        return 0;
    }

    uint8_t volume = s_apu.noise_volume;
    if (volume == 0) {
        return 0;
    }

    uint8_t polynomial = s_apu.regs[0x22 - 0x10];
    uint8_t divisor_code = polynomial & 0x07;
    uint8_t clock_shift = (polynomial >> 4) & 0x0F;
    static const uint16_t divisor_table[8] = {8, 16, 32, 48, 64, 80, 96, 112};
    uint32_t frequency_hz = 524288U / ((uint32_t)divisor_table[divisor_code] << clock_shift);
    if (frequency_hz == 0) {
        frequency_hz = 1;
    }

    s_apu.noise_phase += (uint32_t)(((uint64_t)frequency_hz << 16) / GB_EMU_AUDIO_SAMPLE_RATE);
    while (s_apu.noise_phase >= 0x10000) {
        s_apu.noise_phase -= 0x10000;
        uint16_t feedback = (s_apu.noise_lfsr ^ (s_apu.noise_lfsr >> 1)) & 1;
        s_apu.noise_lfsr = (uint16_t)((s_apu.noise_lfsr >> 1) | (feedback << 14));
        if (polynomial & 0x08) {
            s_apu.noise_lfsr = (uint16_t)((s_apu.noise_lfsr & ~(1U << 6)) | (feedback << 6));
        }
    }

    int32_t sample = (s_apu.noise_lfsr & 1) ? -1 : 1;
    return (int16_t)(sample * (int32_t)volume * GB_EMU_AUDIO_MASTER_GAIN / 15);
}

static int16_t gb_emu_apu_next_sample(void)
{
    gb_emu_apu_clock_frame_sequencer();

    int32_t mixed = 0;
    mixed += gb_emu_apu_mix_square(0);
    mixed += gb_emu_apu_mix_square(1);
    mixed += gb_emu_apu_mix_wave();
    mixed += gb_emu_apu_mix_noise();
    mixed /= 4;

    if (mixed > INT16_MAX) {
        mixed = INT16_MAX;
    } else if (mixed < INT16_MIN) {
        mixed = INT16_MIN;
    }
    return (int16_t)mixed;
}

static esp_err_t gb_emu_audio_init(gb_emu_instance_t *instance)
{
    board_speaker_config_t speaker_config = BOARD_SPEAKER_DEFAULT_CONFIG();
    speaker_config.sample_rate_hz = GB_EMU_AUDIO_SAMPLE_RATE;
    speaker_config.channel_count = GB_EMU_AUDIO_CHANNELS;
    speaker_config.bits_per_sample = GB_EMU_AUDIO_BITS;
    speaker_config.volume = s_output_volume;

    memset(&s_apu, 0, sizeof(s_apu));
    s_apu.noise_lfsr = 0x7FFF;

    esp_err_t ret = board_speaker_init(&speaker_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GB audio disabled, speaker init failed: %s", esp_err_to_name(ret));
        instance->audio_enabled = false;
        return ret;
    }

    instance->audio_enabled = true;
    ESP_LOGI(TAG, "GB audio enabled: %d Hz, %d-bit stereo, volume=%d",
             GB_EMU_AUDIO_SAMPLE_RATE, GB_EMU_AUDIO_BITS, s_output_volume);
#if GB_EMU_AUDIO_STARTUP_TEST
    /*
     * 临时启动自测音：用来区分“扬声器链路没响”和“GB APU 没合成出声音”。
     * 如果这个 1kHz 短音也听不到，优先排查 ES8311/I2S/PA。
     */
    (void)board_speaker_self_test();
#endif
    return ESP_OK;
}

static void gb_emu_audio_render_frame(gb_emu_instance_t *instance)
{
    if (!instance->audio_enabled) {
        return;
    }

    instance->audio_sample_remainder += GB_EMU_AUDIO_SAMPLE_RATE * GB_EMU_FRAME_US;
    uint32_t frame_samples = instance->audio_sample_remainder / 1000000U;
    instance->audio_sample_remainder %= 1000000U;

    if (frame_samples > GB_EMU_AUDIO_MAX_SAMPLES) {
        frame_samples = GB_EMU_AUDIO_MAX_SAMPLES;
    }

    int16_t pcm[GB_EMU_AUDIO_MAX_SAMPLES * GB_EMU_AUDIO_CHANNELS];
    for (uint32_t i = 0; i < frame_samples; i++) {
        int16_t sample = gb_emu_apu_next_sample();
        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
    }

    size_t bytes_written = 0;
    esp_err_t ret = board_speaker_write(pcm,
                                        frame_samples * GB_EMU_AUDIO_CHANNELS * sizeof(int16_t),
                                        &bytes_written,
                                        20);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "speaker write failed: %s", esp_err_to_name(ret));
    }
}

static const char *gb_emu_cartridge_type_name(uint8_t type)
{
    switch (type) {
    case 0x00:
        return "ROM ONLY";
    case 0x01:
        return "MBC1";
    case 0x02:
        return "MBC1+RAM";
    case 0x03:
        return "MBC1+RAM+BATTERY";
    case 0x05:
        return "MBC2";
    case 0x06:
        return "MBC2+BATTERY";
    case 0x08:
        return "ROM+RAM";
    case 0x09:
        return "ROM+RAM+BATTERY";
    case 0x0F:
        return "MBC3+TIMER+BATTERY";
    case 0x10:
        return "MBC3+TIMER+RAM+BATTERY";
    case 0x11:
        return "MBC3";
    case 0x12:
        return "MBC3+RAM";
    case 0x13:
        return "MBC3+RAM+BATTERY";
    case 0x19:
        return "MBC5";
    case 0x1A:
        return "MBC5+RAM";
    case 0x1B:
        return "MBC5+RAM+BATTERY";
    case 0x1C:
        return "MBC5+RUMBLE";
    case 0x1D:
        return "MBC5+RUMBLE+RAM";
    case 0x1E:
        return "MBC5+RUMBLE+RAM+BATTERY";
    default:
        return "UNKNOWN";
    }
}

static size_t gb_emu_expected_rom_size(uint8_t code)
{
    if (code <= 0x08) {
        return (size_t)32 * 1024 * (1U << code);
    }

    switch (code) {
    case 0x52:
        return (size_t)1152 * 1024;
    case 0x53:
        return (size_t)1280 * 1024;
    case 0x54:
        return (size_t)1536 * 1024;
    default:
        return 0;
    }
}

static size_t gb_emu_ram_size(uint8_t code)
{
    switch (code) {
    case 0x00:
        return 0;
    case 0x02:
        return 8 * 1024;
    case 0x03:
        return 32 * 1024;
    case 0x04:
        return 128 * 1024;
    case 0x05:
        return 64 * 1024;
    default:
        return 0;
    }
}

static const char *gb_emu_init_error_name(enum gb_init_error_e error)
{
    switch (error) {
    case GB_INIT_NO_ERROR:
        return "GB_INIT_NO_ERROR";
    case GB_INIT_CARTRIDGE_UNSUPPORTED:
        return "GB_INIT_CARTRIDGE_UNSUPPORTED";
    case GB_INIT_INVALID_CHECKSUM:
        return "GB_INIT_INVALID_CHECKSUM";
    default:
        return "GB_INIT_UNKNOWN";
    }
}

static const char *gb_emu_runtime_error_name(enum gb_error_e error)
{
    switch (error) {
    case GB_UNKNOWN_ERROR:
        return "GB_UNKNOWN_ERROR";
    case GB_INVALID_OPCODE:
        return "GB_INVALID_OPCODE";
    case GB_INVALID_READ:
        return "GB_INVALID_READ";
    case GB_INVALID_WRITE:
        return "GB_INVALID_WRITE";
    default:
        return "GB_ERROR_UNKNOWN";
    }
}

static uint8_t gb_emu_compute_header_checksum(const uint8_t *rom_data)
{
    uint8_t checksum = 0;

    for (uint16_t i = GB_HEADER_TITLE_START; i <= GB_HEADER_MASK_ROM_VERSION; i++) {
        checksum = (uint8_t)(checksum - rom_data[i] - 1);
    }

    return checksum;
}

static void gb_emu_parse_header(const uint8_t *rom_data, gb_emu_rom_header_t *header)
{
    memset(header, 0, sizeof(*header));

    size_t title_len = 0;
    for (uint16_t i = GB_HEADER_TITLE_START; i <= GB_HEADER_TITLE_END && title_len < sizeof(header->title) - 1; i++) {
        uint8_t ch = rom_data[i];
        if (ch == 0) {
            break;
        }
        header->title[title_len++] = (char)ch;
    }
    header->title[title_len] = '\0';

    header->cgb_flag = rom_data[GB_HEADER_CGB_FLAG];
    header->sgb_flag = rom_data[GB_HEADER_SGB_FLAG];
    header->cartridge_type = rom_data[GB_HEADER_CARTRIDGE_TYPE];
    header->rom_size_code = rom_data[GB_HEADER_ROM_SIZE];
    header->ram_size_code = rom_data[GB_HEADER_RAM_SIZE];
    header->destination_code = rom_data[GB_HEADER_DESTINATION_CODE];
    header->mask_rom_version = rom_data[GB_HEADER_MASK_ROM_VERSION];
    header->header_checksum = rom_data[GB_HEADER_HEADER_CHECKSUM];
    header->computed_header_checksum = gb_emu_compute_header_checksum(rom_data);
    header->expected_rom_size = gb_emu_expected_rom_size(header->rom_size_code);
    header->header_checksum_ok = header->header_checksum == header->computed_header_checksum;
}

esp_err_t gb_emu_load_rom(const char *path, gb_emu_rom_t *rom)
{
    if (path == NULL || rom == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(rom, 0, sizeof(*rom));

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open GB ROM: %s", path);
        return ESP_FAIL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    long file_size = ftell(file);
    if (file_size < GB_MIN_ROM_SIZE) {
        fclose(file);
        ESP_LOGE(TAG, "Invalid GB ROM size: %ld bytes", file_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    uint8_t *data = heap_caps_malloc((size_t)file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) {
        ESP_LOGW(TAG, "PSRAM allocation failed, trying generic 8-bit heap");
        data = heap_caps_malloc((size_t)file_size, MALLOC_CAP_8BIT);
    }

    if (data == NULL) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for GB ROM", file_size);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = fread(data, 1, (size_t)file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        heap_caps_free(data);
        ESP_LOGE(TAG, "Short read: expected %ld bytes, got %u bytes", file_size, (unsigned)bytes_read);
        return ESP_FAIL;
    }

    rom->data = data;
    rom->size = (size_t)file_size;
    gb_emu_parse_header(rom->data, &rom->header);

    return ESP_OK;
}

void gb_emu_free_rom(gb_emu_rom_t *rom)
{
    if (rom == NULL) {
        return;
    }

    if (rom->data != NULL) {
        heap_caps_free(rom->data);
    }

    memset(rom, 0, sizeof(*rom));
}

void gb_emu_set_joypad(uint8_t joypad)
{
    if (s_instance == NULL) {
        return;
    }

    s_instance->gb.direct.joypad = joypad;
}

void gb_emu_set_volume(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }

    s_output_volume = volume;
    if (board_speaker_set_volume(volume) != ESP_OK) {
        ESP_LOGW(TAG, "set GB speaker volume failed");
    }
}

uint8_t gb_emu_get_volume(void)
{
    return s_output_volume;
}

void gb_emu_log_rom_info(const gb_emu_rom_t *rom)
{
    if (rom == NULL || rom->data == NULL) {
        ESP_LOGW(TAG, "No GB ROM loaded");
        return;
    }

    const gb_emu_rom_header_t *header = &rom->header;
    ESP_LOGI(TAG, "GB ROM loaded: %u bytes", (unsigned)rom->size);
    ESP_LOGI(TAG, "Title: %s", header->title[0] != '\0' ? header->title : "(empty)");
    ESP_LOGI(TAG, "Cartridge: 0x%02X (%s)", header->cartridge_type, gb_emu_cartridge_type_name(header->cartridge_type));
    ESP_LOGI(TAG, "ROM size code: 0x%02X, expected size: %u bytes", header->rom_size_code, (unsigned)header->expected_rom_size);
    ESP_LOGI(TAG, "RAM size code: 0x%02X, external RAM: %u bytes", header->ram_size_code, (unsigned)gb_emu_ram_size(header->ram_size_code));
    ESP_LOGI(TAG, "CGB flag: 0x%02X, SGB flag: 0x%02X, destination: %s",
             header->cgb_flag,
             header->sgb_flag,
             header->destination_code == 0 ? "Japan" : "Overseas");
    ESP_LOGI(TAG, "Header checksum: stored=0x%02X computed=0x%02X %s",
             header->header_checksum,
             header->computed_header_checksum,
             header->header_checksum_ok ? "OK" : "FAILED");

    if (header->expected_rom_size != 0 && header->expected_rom_size != rom->size) {
        ESP_LOGW(TAG, "ROM file size does not match header expected size");
    }
}

static uint8_t gb_emu_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
    gb_emu_instance_t *instance = (gb_emu_instance_t *)gb->direct.priv;

    if (instance == NULL || instance->rom == NULL || instance->rom->data == NULL || addr >= instance->rom->size) {
        return 0xFF;
    }

    return instance->rom->data[addr];
}

static uint8_t gb_emu_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
    gb_emu_instance_t *instance = (gb_emu_instance_t *)gb->direct.priv;

    if (instance == NULL || instance->cart_ram == NULL || instance->cart_ram_size == 0) {
        return 0xFF;
    }

    return instance->cart_ram[addr % instance->cart_ram_size];
}

static void gb_emu_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val)
{
    gb_emu_instance_t *instance = (gb_emu_instance_t *)gb->direct.priv;

    if (instance == NULL || instance->cart_ram == NULL || instance->cart_ram_size == 0) {
        return;
    }

    instance->cart_ram[addr % instance->cart_ram_size] = val;
}

static void gb_emu_error(struct gb_s *gb, const enum gb_error_e error, const uint16_t addr)
{
    (void)gb;
    ESP_LOGE(TAG, "Peanut-GB runtime error: %s at 0x%04X", gb_emu_runtime_error_name(error), addr);
}

static void gb_emu_lcd_draw_line(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line)
{
    gb_emu_instance_t *instance = (gb_emu_instance_t *)gb->direct.priv;

    if (line >= GB_EMU_SCREEN_HEIGHT) {
        return;
    }

    uint16_t display_y = GB_EMU_DISPLAY_Y_OFFSET + (uint16_t)line * GB_EMU_DISPLAY_SCALE;
    (void)board_rlcd_draw_gb_line_2x(GB_EMU_DISPLAY_X_OFFSET, display_y, pixels, GB_EMU_SCREEN_WIDTH);

    if (instance != NULL) {
        instance->frame_dirty = true;
    }
}

static void gb_emu_task(void *arg)
{
    gb_emu_instance_t *instance = (gb_emu_instance_t *)arg;
    int64_t next_frame_us = esp_timer_get_time();
    int64_t last_log_us = next_frame_us;
    uint64_t interval_emu_us = 0;
    uint64_t interval_flush_us = 0;
    uint32_t interval_frames = 0;
    uint32_t interval_flushes = 0;

    ESP_LOGI(TAG, "GB emulation task started");

    while (1) {
        if (instance->stop_requested) {
            break;
        }

        int64_t emu_start_us = esp_timer_get_time();
        gb_run_frame(&instance->gb);
        int64_t emu_end_us = esp_timer_get_time();
        instance->frame_count++;
        interval_frames++;
        interval_emu_us += (uint64_t)(emu_end_us - emu_start_us);

        gb_emu_audio_render_frame(instance);

        if (instance->frame_dirty) {
            int64_t flush_start_us = esp_timer_get_time();
            (void)board_rlcd_flush();
            int64_t flush_end_us = esp_timer_get_time();

            instance->frame_dirty = false;
            instance->flush_count++;
            interval_flushes++;
            interval_flush_us += (uint64_t)(flush_end_us - flush_start_us);
        }

#if GB_EMU_ENABLE_PERF_LOG
        if ((instance->frame_count % 60) == 0) {
            int64_t now_us = esp_timer_get_time();
            uint64_t elapsed_us = (uint64_t)(now_us - last_log_us);
            uint32_t fps_x10 = elapsed_us > 0 ? (uint32_t)((uint64_t)interval_frames * 10000000ULL / elapsed_us) : 0;
            uint32_t emu_avg_us = interval_frames > 0 ? (uint32_t)(interval_emu_us / interval_frames) : 0;
            uint32_t flush_avg_us = interval_flushes > 0 ? (uint32_t)(interval_flush_us / interval_flushes) : 0;

            ESP_LOGI(TAG,
                     "GB perf: frames=%u flushes=%u fps=%u.%u emu_avg=%uus flush_avg=%uus",
                     (unsigned)instance->frame_count,
                     (unsigned)instance->flush_count,
                     (unsigned)(fps_x10 / 10),
                     (unsigned)(fps_x10 % 10),
                     (unsigned)emu_avg_us,
                     (unsigned)flush_avg_us);

            last_log_us = now_us;
            interval_emu_us = 0;
            interval_flush_us = 0;
            interval_frames = 0;
            interval_flushes = 0;
        }
#else
        (void)last_log_us;
        (void)interval_emu_us;
        (void)interval_flush_us;
        (void)interval_frames;
        (void)interval_flushes;
#endif

        /*
         * DMG Game Boy 大约 59.7 FPS，即一帧约 16742us。
         *
         * 如果当前帧跑得很快，就延时到下一帧；
         * 如果当前帧已经超时，也必须主动让出 CPU，否则 idle 任务无法运行，
         * FreeRTOS 的 task watchdog 会认为 CPU 被 gb_emu 长时间占住。
         */
        next_frame_us += GB_EMU_FRAME_US;
        int64_t now_us = esp_timer_get_time();
        int64_t delay_us = next_frame_us - now_us;

        if (delay_us > 1000) {
            vTaskDelay(pdMS_TO_TICKS((uint32_t)(delay_us / 1000)));
        } else {
            next_frame_us = now_us;
            /*
             * 当前工程 FreeRTOS tick 是 100Hz，vTaskDelay(1) 实际约等于 10ms。
             * 如果每帧都 delay 1 tick，20ms 的模拟耗时会被硬生生变成约 30ms，
             * 帧率就会卡在 33 FPS 左右。
             *
             * 这里大多数超时帧只主动让出同优先级调度机会；每隔一小段时间
             * 再真正 delay 1 tick，让 CPU1 的 idle 任务有机会运行，避免任务看门狗报警。
             */
            if ((instance->frame_count % GB_EMU_IDLE_DELAY_INTERVAL) == 0) {
                vTaskDelay(1);
            } else {
                taskYIELD();
            }
        }
    }

    memset(&s_apu, 0, sizeof(s_apu));
    gb_emu_set_joypad(0xFF);
    if (instance->cart_ram != NULL) {
        heap_caps_free(instance->cart_ram);
    }
    s_instance = NULL;
    ESP_LOGI(TAG, "GB emulation task stopped");
    heap_caps_free(instance);
    vTaskDelete(NULL);
}

esp_err_t gb_emu_start(const gb_emu_rom_t *rom)
{
    if (rom == NULL || rom->data == NULL || rom->size < GB_MIN_ROM_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!board_rlcd_is_initialized()) {
        ESP_LOGE(TAG, "RLCD is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_instance != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    gb_emu_instance_t *instance = heap_caps_calloc(1, sizeof(gb_emu_instance_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (instance == NULL) {
        return ESP_ERR_NO_MEM;
    }

    instance->rom = rom;
    instance->cart_ram_size = gb_emu_ram_size(rom->header.ram_size_code);
    if (instance->cart_ram_size > 0) {
        instance->cart_ram = heap_caps_calloc(1, instance->cart_ram_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (instance->cart_ram == NULL) {
            instance->cart_ram = heap_caps_calloc(1, instance->cart_ram_size, MALLOC_CAP_8BIT);
        }

        if (instance->cart_ram == NULL) {
            heap_caps_free(instance);
            return ESP_ERR_NO_MEM;
        }
    }

    enum gb_init_error_e init_error = gb_init(&instance->gb,
                                              gb_emu_rom_read,
                                              gb_emu_cart_ram_read,
                                              gb_emu_cart_ram_write,
                                              gb_emu_error,
                                              instance);
    if (init_error != GB_INIT_NO_ERROR) {
        ESP_LOGE(TAG, "Peanut-GB init failed: %s", gb_emu_init_error_name(init_error));
        if (instance->cart_ram != NULL) {
            heap_caps_free(instance->cart_ram);
        }
        heap_caps_free(instance);
        return ESP_FAIL;
    }

    gb_init_lcd(&instance->gb, gb_emu_lcd_draw_line);
    instance->gb.direct.joypad = 0xFF;
    (void)gb_emu_audio_init(instance);

    ESP_ERROR_CHECK(board_rlcd_clear(BOARD_RLCD_COLOR_BLACK));
    ESP_LOGI(TAG, "Starting GB emulator: display %dx%d -> %dx%d at (%d,%d)",
             GB_EMU_SCREEN_WIDTH,
             GB_EMU_SCREEN_HEIGHT,
             GB_EMU_SCREEN_WIDTH * GB_EMU_DISPLAY_SCALE,
             GB_EMU_SCREEN_HEIGHT * GB_EMU_DISPLAY_SCALE,
             GB_EMU_DISPLAY_X_OFFSET,
             GB_EMU_DISPLAY_Y_OFFSET);

    BaseType_t task_ret = xTaskCreatePinnedToCore(gb_emu_task,
                                                  "gb_emu",
                                                  GB_EMU_TASK_STACK_SIZE,
                                                  instance,
                                                  GB_EMU_TASK_PRIORITY,
                                                  NULL,
                                                  GB_EMU_TASK_CORE);
    if (task_ret != pdPASS) {
        if (instance->cart_ram != NULL) {
            heap_caps_free(instance->cart_ram);
        }
        heap_caps_free(instance);
        return ESP_ERR_NO_MEM;
    }

    s_instance = instance;
    return ESP_OK;
}

esp_err_t gb_emu_stop(void)
{
    gb_emu_instance_t *instance = s_instance;
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
