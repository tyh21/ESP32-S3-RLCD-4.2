#include "board_speaker.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
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

#define BOARD_SPEAKER_I2C_PORT      I2C_NUM_0
#define BOARD_SPEAKER_I2C_SDA       GPIO_NUM_13
#define BOARD_SPEAKER_I2C_SCL       GPIO_NUM_14

#define BOARD_SPEAKER_I2S_MCLK      GPIO_NUM_16
#define BOARD_SPEAKER_I2S_BCLK      GPIO_NUM_9
#define BOARD_SPEAKER_I2S_WS        GPIO_NUM_45
#define BOARD_SPEAKER_I2S_DOUT      GPIO_NUM_8

#define BOARD_SPEAKER_PA_PIN        GPIO_NUM_46
#define BOARD_SPEAKER_PA_GAIN       6
#define BOARD_SPEAKER_WRITE_TIMEOUT_MS 1000

static const char *TAG = "board_speaker";

static i2c_master_bus_handle_t s_i2c_bus;
static bool s_i2c_bus_owner;
static i2s_chan_handle_t s_i2s_tx;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_codec_if;
static esp_codec_dev_handle_t s_play_dev;
static board_speaker_config_t s_config = BOARD_SPEAKER_DEFAULT_CONFIG();

static esp_err_t board_speaker_init_i2c(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    /*
     * I2C 只负责配置 ES8311 这颗音频 codec。
     * 真正的音频 PCM 数据走 I2S，不走 I2C。
     */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_SPEAKER_I2C_PORT,
        .sda_io_num = BOARD_SPEAKER_I2C_SDA,
        .scl_io_num = BOARD_SPEAKER_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (ret == ESP_OK) {
        s_i2c_bus_owner = true;
    }
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = i2c_master_get_bus_handle(BOARD_SPEAKER_I2C_PORT, &s_i2c_bus);
        if (ret == ESP_OK) {
            s_i2c_bus_owner = false;
        }
    }
    return ret;
}

static esp_err_t board_speaker_init_i2s(uint32_t sample_rate_hz)
{
    if (s_i2s_tx != NULL) {
        return ESP_OK;
    }

    /*
     * ESP32-S3 做 I2S 主机，输出 MCLK/BCLK/WS/DOUT。
     * ES8311 接收 DOUT 上的数字音频，再转换成模拟信号给功放。
     */
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&channel_config, &s_i2s_tx, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    i2s_tdm_slot_mask_t slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO, slot_mask),
        .gpio_cfg = {
            .mclk = BOARD_SPEAKER_I2S_MCLK,
            .bclk = BOARD_SPEAKER_I2S_BCLK,
            .ws = BOARD_SPEAKER_I2S_WS,
            .dout = BOARD_SPEAKER_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    /*
     * ES8311 使用 MCLK，当前 TDM slot 宽度是 32bit。
     * IDF 默认的 256 倍 MCLK 在这种配置下会被驱动警告并自动改成 384，
     * 这里显式设置成 384，让时钟配置和驱动实际行为一致。
     */
    tdm_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    tdm_config.slot_cfg.total_slot = 4;

    ret = i2s_channel_init_tdm_mode(s_i2s_tx, &tdm_config);
    if (ret != ESP_OK) {
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
        return ret;
    }

    return i2s_channel_enable(s_i2s_tx);
}

esp_err_t board_speaker_init(const board_speaker_config_t *config)
{
    if (s_play_dev != NULL) {
        return ESP_OK;
    }

    if (config != NULL) {
        s_config = *config;
    }

    ESP_LOGI(TAG, "Initializing ES8311 speaker: I2C SDA=%d SCL=%d, I2S MCLK=%d BCLK=%d WS=%d DOUT=%d PA=%d",
             BOARD_SPEAKER_I2C_SDA, BOARD_SPEAKER_I2C_SCL,
             BOARD_SPEAKER_I2S_MCLK, BOARD_SPEAKER_I2S_BCLK, BOARD_SPEAKER_I2S_WS,
             BOARD_SPEAKER_I2S_DOUT, BOARD_SPEAKER_PA_PIN);

    ESP_RETURN_ON_ERROR(board_speaker_init_i2c(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(board_speaker_init_i2s(s_config.sample_rate_hz), TAG, "I2S init failed");

    audio_codec_i2s_cfg_t i2s_config = {
        .tx_handle = s_i2s_tx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_config);
    if (s_data_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    audio_codec_i2c_cfg_t i2c_config = {
        .port = BOARD_SPEAKER_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
    if (s_ctrl_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_gpio_if = audio_codec_new_gpio();
    if (s_gpio_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    es8311_codec_cfg_t es8311_config = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BOARD_SPEAKER_PA_PIN,
        .use_mclk = true,
        .hw_gain = {
            .pa_gain = BOARD_SPEAKER_PA_GAIN,
        },
    };
    s_codec_if = es8311_codec_new(&es8311_config);
    if (s_codec_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_cfg_t dev_config = {
        .codec_if = s_codec_if,
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
        ESP_LOGE(TAG, "esp_codec_dev_open failed: %d", ret);
        return ESP_FAIL;
    }

    return board_speaker_set_volume(s_config.volume);
}

esp_err_t board_speaker_set_volume(uint8_t volume)
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

    s_config.volume = volume;
    return ESP_OK;
}

uint8_t board_speaker_get_volume(void)
{
    return s_config.volume;
}

esp_err_t board_speaker_write(const void *buffer, size_t buffer_size, size_t *bytes_written, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (s_play_dev == NULL || buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
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

esp_err_t board_speaker_self_test(void)
{
    board_speaker_config_t config = BOARD_SPEAKER_DEFAULT_CONFIG();
    config.sample_rate_hz = 16000;
    config.channel_count = 2;
    config.bits_per_sample = 16;
    config.volume = 55;

    esp_err_t ret = board_speaker_init(&config);
    if (ret != ESP_OK) {
        return ret;
    }

    const uint32_t duration_ms = 800;
    const uint32_t frequency_hz = 1000;
    const size_t frame_count = config.sample_rate_hz * duration_ms / 1000;
    const size_t sample_count = frame_count * config.channel_count;
    int16_t *pcm = (int16_t *)heap_caps_malloc(sample_count * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /*
     * 生成一个 1 kHz 的双声道正弦测试音。
     * 幅度不要太大，避免第一次测试时声音过响。
     */
    const float amplitude = 6000.0f;
    for (size_t i = 0; i < frame_count; i++) {
        float phase = 2.0f * (float)M_PI * (float)frequency_hz * (float)i / (float)config.sample_rate_hz;
        int16_t sample = (int16_t)(sinf(phase) * amplitude);
        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
    }

    size_t bytes_written = 0;
    ret = board_speaker_write(pcm, sample_count * sizeof(int16_t), &bytes_written, BOARD_SPEAKER_WRITE_TIMEOUT_MS);
    free(pcm);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Speaker self-test wrote %u bytes", (unsigned)bytes_written);
    }
    return ret;
}

void board_speaker_deinit(void)
{
    if (s_play_dev != NULL) {
        esp_codec_dev_close(s_play_dev);
        esp_codec_dev_delete(s_play_dev);
        s_play_dev = NULL;
    }
    if (s_codec_if != NULL) {
        audio_codec_delete_codec_if(s_codec_if);
        s_codec_if = NULL;
    }
    if (s_gpio_if != NULL) {
        audio_codec_delete_gpio_if(s_gpio_if);
        s_gpio_if = NULL;
    }
    if (s_ctrl_if != NULL) {
        audio_codec_delete_ctrl_if(s_ctrl_if);
        s_ctrl_if = NULL;
    }
    if (s_data_if != NULL) {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = NULL;
    }
    if (s_i2s_tx != NULL) {
        /*
         * esp_codec_dev_close() 会通过 data_if 关闭底层 I2S channel。
         * 这里如果再次 i2s_channel_disable()，IDF 会先打印
         * "the channel has not been enabled yet"，即使我们随后忽略返回值也没用。
         * 所以释放阶段只删除 channel。
         */
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
    }
    if (s_i2c_bus != NULL && s_i2c_bus_owner) {
        i2c_del_master_bus(s_i2c_bus);
    }
    s_i2c_bus = NULL;
    s_i2c_bus_owner = false;
}
