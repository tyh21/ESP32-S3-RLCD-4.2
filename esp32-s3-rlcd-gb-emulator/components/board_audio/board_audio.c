#include "board_audio.h"

#include <math.h>
#include <stdlib.h>
#include "driver/i2c_master.h"
#include "driver/i2s_tdm.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BOARD_AUDIO_I2C_PORT      I2C_NUM_0
#define BOARD_AUDIO_I2C_SDA       GPIO_NUM_13
#define BOARD_AUDIO_I2C_SCL       GPIO_NUM_14

#define BOARD_AUDIO_I2S_MCLK      GPIO_NUM_16
#define BOARD_AUDIO_I2S_BCLK      GPIO_NUM_9
#define BOARD_AUDIO_I2S_WS        GPIO_NUM_45
#define BOARD_AUDIO_I2S_DIN       GPIO_NUM_10
#define BOARD_AUDIO_I2S_DOUT      GPIO_NUM_8

#define BOARD_AUDIO_PA_PIN        GPIO_NUM_46
#define BOARD_AUDIO_PA_GAIN       6
#define BOARD_AUDIO_TIMEOUT_MS    1000

static const char *TAG = "board_audio";

static i2c_master_bus_handle_t s_i2c_bus;
static i2s_chan_handle_t s_i2s_tx;
static i2s_chan_handle_t s_i2s_rx;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_in_ctrl_if;
static const audio_codec_ctrl_if_t *s_out_ctrl_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_in_codec_if;
static const audio_codec_if_t *s_out_codec_if;
static esp_codec_dev_handle_t s_record_dev;
static esp_codec_dev_handle_t s_play_dev;
static board_audio_config_t s_config = BOARD_AUDIO_DEFAULT_CONFIG();

static esp_err_t board_audio_init_i2c(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_AUDIO_I2C_PORT,
        .sda_io_num = BOARD_AUDIO_I2C_SDA,
        .scl_io_num = BOARD_AUDIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = i2c_master_get_bus_handle(BOARD_AUDIO_I2C_PORT, &s_i2c_bus);
    }
    return ret;
}

static esp_err_t board_audio_init_i2s(uint32_t sample_rate_hz)
{
    if (s_i2s_tx != NULL && s_i2s_rx != NULL) {
        return ESP_OK;
    }

    /*
     * 这块板子的录音和播放共用同一组 I2S 时钟。
     * TX: ESP32-S3 -> ES8311 -> 功放/扬声器
     * RX: ES7210 -> ESP32-S3
     */
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&channel_config, &s_i2s_tx, &s_i2s_rx);
    if (ret != ESP_OK) {
        return ret;
    }

    i2s_tdm_slot_mask_t slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO, slot_mask),
        .gpio_cfg = {
            .mclk = BOARD_AUDIO_I2S_MCLK,
            .bclk = BOARD_AUDIO_I2S_BCLK,
            .ws = BOARD_AUDIO_I2S_WS,
            .dout = BOARD_AUDIO_I2S_DOUT,
            .din = BOARD_AUDIO_I2S_DIN,
        },
    };
    /*
     * 当前全双工音频使用 32bit TDM slot，并且 ES8311 需要 MCLK。
     * 显式使用 384 倍 MCLK，避免 IDF 在运行时警告并临时调整。
     */
    tdm_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    tdm_config.slot_cfg.total_slot = 4;

    ret = i2s_channel_init_tdm_mode(s_i2s_tx, &tdm_config);
    if (ret != ESP_OK) {
        goto fail;
    }

    ret = i2s_channel_init_tdm_mode(s_i2s_rx, &tdm_config);
    if (ret != ESP_OK) {
        goto fail;
    }

    ret = i2s_channel_enable(s_i2s_tx);
    if (ret != ESP_OK) {
        goto fail;
    }

    ret = i2s_channel_enable(s_i2s_rx);
    if (ret != ESP_OK) {
        goto fail;
    }

    return ESP_OK;

fail:
    if (s_i2s_tx != NULL) {
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
    }
    if (s_i2s_rx != NULL) {
        i2s_del_channel(s_i2s_rx);
        s_i2s_rx = NULL;
    }
    return ret;
}

static esp_err_t board_audio_init_data_if(void)
{
    audio_codec_i2s_cfg_t i2s_config = {
        .tx_handle = s_i2s_tx,
        .rx_handle = s_i2s_rx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_config);
    return s_data_if == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

static esp_err_t board_audio_init_record_dev(void)
{
    audio_codec_i2c_cfg_t i2c_config = {
        .port = BOARD_AUDIO_I2C_PORT,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    s_in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
    if (s_in_ctrl_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    es7210_codec_cfg_t es7210_config = {
        .ctrl_if = s_in_ctrl_if,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    s_in_codec_if = es7210_codec_new(&es7210_config);
    if (s_in_codec_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_cfg_t dev_config = {
        .codec_if = s_in_codec_if,
        .data_if = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    s_record_dev = esp_codec_dev_new(&dev_config);
    if (s_record_dev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = (int)s_config.sample_rate_hz,
        .channel = s_config.channel_count,
        .bits_per_sample = s_config.bits_per_sample,
    };

    int ret = esp_codec_dev_open(s_record_dev, &sample_info);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "open record device failed: %d", ret);
        return ESP_FAIL;
    }

    return board_audio_set_mic_gain(s_config.mic_gain_db);
}

static esp_err_t board_audio_init_play_dev(void)
{
    audio_codec_i2c_cfg_t i2c_config = {
        .port = BOARD_AUDIO_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    s_out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
    if (s_out_ctrl_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_gpio_if = audio_codec_new_gpio();
    if (s_gpio_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    es8311_codec_cfg_t es8311_config = {
        .ctrl_if = s_out_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BOARD_AUDIO_PA_PIN,
        .use_mclk = true,
        .hw_gain = {
            .pa_gain = BOARD_AUDIO_PA_GAIN,
        },
    };
    s_out_codec_if = es8311_codec_new(&es8311_config);
    if (s_out_codec_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_cfg_t dev_config = {
        .codec_if = s_out_codec_if,
        .data_if = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_play_dev = esp_codec_dev_new(&dev_config);
    if (s_play_dev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = (int)s_config.sample_rate_hz,
        .channel = s_config.channel_count,
        .bits_per_sample = s_config.bits_per_sample,
    };

    int ret = esp_codec_dev_open(s_play_dev, &sample_info);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "open playback device failed: %d", ret);
        return ESP_FAIL;
    }

    return board_audio_set_speaker_volume(s_config.speaker_volume);
}

esp_err_t board_audio_init(const board_audio_config_t *config)
{
    if (s_record_dev != NULL) {
        return ESP_OK;
    }

    if (config != NULL) {
        s_config = *config;
    }

    ESP_LOGI(TAG, "Initializing full-duplex audio: I2C SDA=%d SCL=%d, I2S MCLK=%d BCLK=%d WS=%d DIN=%d DOUT=%d PA=%d",
             BOARD_AUDIO_I2C_SDA, BOARD_AUDIO_I2C_SCL,
             BOARD_AUDIO_I2S_MCLK, BOARD_AUDIO_I2S_BCLK, BOARD_AUDIO_I2S_WS,
             BOARD_AUDIO_I2S_DIN, BOARD_AUDIO_I2S_DOUT, BOARD_AUDIO_PA_PIN);

    ESP_RETURN_ON_ERROR(board_audio_init_i2c(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(board_audio_init_i2s(s_config.sample_rate_hz), TAG, "I2S init failed");
    ESP_RETURN_ON_ERROR(board_audio_init_data_if(), TAG, "I2S data interface init failed");
    ESP_RETURN_ON_ERROR(board_audio_init_record_dev(), TAG, "record device init failed");

    ESP_LOGI(TAG, "Audio input initialized: %lu Hz, %u channels, %u bits, mic gain %.1f dB",
             s_config.sample_rate_hz, s_config.channel_count, s_config.bits_per_sample,
             s_config.mic_gain_db);
    return ESP_OK;
}

esp_err_t board_audio_read_mic(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (s_record_dev == NULL || buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int ret = esp_codec_dev_read(s_record_dev, buffer, (int)buffer_size);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }

    if (bytes_read != NULL) {
        *bytes_read = buffer_size;
    }
    return ESP_OK;
}

esp_err_t board_audio_write_speaker(const void *buffer, size_t buffer_size, size_t *bytes_written, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_play_dev == NULL) {
        ESP_RETURN_ON_ERROR(board_audio_init(NULL), TAG, "audio input init failed");
        ESP_RETURN_ON_ERROR(board_audio_init_play_dev(), TAG, "playback device init failed");
    }

    int ret = esp_codec_dev_write(s_play_dev, (void *)buffer, (int)buffer_size);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }

    if (bytes_written != NULL) {
        *bytes_written = buffer_size;
    }
    return ESP_OK;
}

esp_err_t board_audio_set_mic_gain(float gain_db)
{
    if (s_record_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int ret = esp_codec_dev_set_in_gain(s_record_dev, gain_db);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }

    s_config.mic_gain_db = gain_db;
    return ESP_OK;
}

esp_err_t board_audio_set_speaker_volume(uint8_t volume)
{
    if (s_play_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (volume > 100) {
        volume = 100;
    }

    int ret = esp_codec_dev_set_out_vol(s_play_dev, volume);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }

    s_config.speaker_volume = volume;
    return ESP_OK;
}

esp_err_t board_audio_speaker_self_test(void)
{
    esp_err_t ret = board_audio_init(NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    const uint32_t duration_ms = 600;
    const uint32_t frequency_hz = 1000;
    const size_t frame_count = s_config.sample_rate_hz * duration_ms / 1000;
    const size_t sample_count = frame_count * s_config.channel_count;
    int16_t *pcm = (int16_t *)heap_caps_malloc(sample_count * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const float amplitude = 6000.0f;
    for (size_t i = 0; i < frame_count; i++) {
        float phase = 2.0f * (float)M_PI * (float)frequency_hz * (float)i / (float)s_config.sample_rate_hz;
        int16_t sample = (int16_t)(sinf(phase) * amplitude);
        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
    }

    size_t bytes_written = 0;
    ret = board_audio_write_speaker(pcm, sample_count * sizeof(int16_t), &bytes_written, BOARD_AUDIO_TIMEOUT_MS);
    free(pcm);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Speaker self-test wrote %u bytes", (unsigned)bytes_written);
    }
    return ret;
}

void board_audio_deinit(void)
{
    if (s_record_dev != NULL) {
        esp_codec_dev_close(s_record_dev);
        esp_codec_dev_delete(s_record_dev);
        s_record_dev = NULL;
    }
    if (s_play_dev != NULL) {
        esp_codec_dev_close(s_play_dev);
        esp_codec_dev_delete(s_play_dev);
        s_play_dev = NULL;
    }
    if (s_in_codec_if != NULL) {
        audio_codec_delete_codec_if(s_in_codec_if);
        s_in_codec_if = NULL;
    }
    if (s_out_codec_if != NULL) {
        audio_codec_delete_codec_if(s_out_codec_if);
        s_out_codec_if = NULL;
    }
    if (s_gpio_if != NULL) {
        audio_codec_delete_gpio_if(s_gpio_if);
        s_gpio_if = NULL;
    }
    if (s_in_ctrl_if != NULL) {
        audio_codec_delete_ctrl_if(s_in_ctrl_if);
        s_in_ctrl_if = NULL;
    }
    if (s_out_ctrl_if != NULL) {
        audio_codec_delete_ctrl_if(s_out_ctrl_if);
        s_out_ctrl_if = NULL;
    }
    if (s_data_if != NULL) {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = NULL;
    }
    if (s_i2s_rx != NULL) {
        i2s_channel_disable(s_i2s_rx);
        i2s_del_channel(s_i2s_rx);
        s_i2s_rx = NULL;
    }
    if (s_i2s_tx != NULL) {
        i2s_channel_disable(s_i2s_tx);
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
    }
    if (s_i2c_bus != NULL) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}
