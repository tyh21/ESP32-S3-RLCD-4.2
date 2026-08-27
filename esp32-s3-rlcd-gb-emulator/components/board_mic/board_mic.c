#include "board_mic.h"

#include <stdlib.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/i2s_tdm.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define BOARD_MIC_I2C_PORT      I2C_NUM_0
#define BOARD_MIC_I2C_SDA       GPIO_NUM_13
#define BOARD_MIC_I2C_SCL       GPIO_NUM_14
#define BOARD_MIC_I2C_SPEED_HZ  400000

#define BOARD_MIC_I2S_MCLK      GPIO_NUM_16
#define BOARD_MIC_I2S_BCLK      GPIO_NUM_9
#define BOARD_MIC_I2S_WS        GPIO_NUM_45
#define BOARD_MIC_I2S_DIN       GPIO_NUM_10
#define BOARD_MIC_I2S_DOUT      GPIO_NUM_8

#define BOARD_MIC_READ_TIMEOUT_MS 1000

static const char *TAG = "board_mic";

static i2c_master_bus_handle_t s_i2c_bus;
static i2s_chan_handle_t s_i2s_tx;
static i2s_chan_handle_t s_i2s_rx;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_if_t *s_codec_if;
static esp_codec_dev_handle_t s_record_dev;
static board_mic_config_t s_config = BOARD_MIC_DEFAULT_CONFIG();

static esp_err_t board_mic_init_i2c(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    /*
     * I2C 只负责“配置 ES7210 芯片”。
     * 真正的声音数据不会走 I2C，而是走下面的 I2S。
     */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_MIC_I2C_PORT,
        .sda_io_num = BOARD_MIC_I2C_SDA,
        .scl_io_num = BOARD_MIC_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = i2c_master_get_bus_handle(BOARD_MIC_I2C_PORT, &s_i2c_bus);
    }
    return ret;
}

static esp_err_t board_mic_init_i2s(uint32_t sample_rate_hz)
{
    if (s_i2s_rx != NULL) {
        return ESP_OK;
    }

    /*
     * I2S 是声音数据通道。
     * S3 做主机，输出 MCLK/BCLK/WS 三个时钟；
     * ES7210 按这些时钟把麦克风采样数据从 DIN(GPIO10) 送回 S3。
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
            .mclk = BOARD_MIC_I2S_MCLK,
            .bclk = BOARD_MIC_I2S_BCLK,
            .ws = BOARD_MIC_I2S_WS,
            .dout = BOARD_MIC_I2S_DOUT,
            .din = BOARD_MIC_I2S_DIN,
        },
    };

    /*
     * ES7210 是四通道 ADC。官方示例也使用 4 个 TDM slot。
     * 当前板子只焊了两个麦克风，上层以 2 声道 PCM 读取即可。
     */
    tdm_config.slot_cfg.total_slot = 4;

    ret = i2s_channel_init_tdm_mode(s_i2s_tx, &tdm_config);
    if (ret != ESP_OK) {
        i2s_del_channel(s_i2s_tx);
        i2s_del_channel(s_i2s_rx);
        s_i2s_tx = NULL;
        s_i2s_rx = NULL;
        return ret;
    }

    ret = i2s_channel_init_tdm_mode(s_i2s_rx, &tdm_config);
    if (ret != ESP_OK) {
        i2s_del_channel(s_i2s_tx);
        i2s_del_channel(s_i2s_rx);
        s_i2s_tx = NULL;
        s_i2s_rx = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_i2s_tx);
    if (ret != ESP_OK) {
        i2s_del_channel(s_i2s_tx);
        i2s_del_channel(s_i2s_rx);
        s_i2s_tx = NULL;
        s_i2s_rx = NULL;
        return ret;
    }

    return i2s_channel_enable(s_i2s_rx);
}

esp_err_t board_mic_init(const board_mic_config_t *config)
{
    if (s_record_dev != NULL) {
        return ESP_OK;
    }

    if (config != NULL) {
        s_config = *config;
    }

    ESP_LOGI(TAG, "Initializing ES7210 microphones: I2C SDA=%d SCL=%d, I2S MCLK=%d BCLK=%d WS=%d DIN=%d DOUT=%d",
             BOARD_MIC_I2C_SDA, BOARD_MIC_I2C_SCL,
             BOARD_MIC_I2S_MCLK, BOARD_MIC_I2S_BCLK, BOARD_MIC_I2S_WS, BOARD_MIC_I2S_DIN, BOARD_MIC_I2S_DOUT);

    ESP_RETURN_ON_ERROR(board_mic_init_i2c(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(board_mic_init_i2s(s_config.sample_rate_hz), TAG, "I2S init failed");

    audio_codec_i2s_cfg_t i2s_config = {
        .tx_handle = s_i2s_tx,
        .rx_handle = s_i2s_rx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_config);
    if (s_data_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    audio_codec_i2c_cfg_t i2c_config = {
        .port = BOARD_MIC_I2C_PORT,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
    if (s_ctrl_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /*
     * 这块板子的两个贴片麦克风接在 ES7210 的 MIC1/MIC2。
     * MIC3 是 AEC 参考输入，MIC4 在当前原理图中没有接实际信号。
     * I2S 侧保留 4 个 TDM slot，是为了和 ES7210 的多通道时序保持一致。
     */
    es7210_codec_cfg_t es7210_config = {
        .ctrl_if = s_ctrl_if,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    s_codec_if = es7210_codec_new(&es7210_config);
    if (s_codec_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_cfg_t dev_config = {
        .codec_if = s_codec_if,
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
        ESP_LOGE(TAG, "esp_codec_dev_open failed: %d", ret);
        return ESP_FAIL;
    }

    ret = esp_codec_dev_set_in_gain(s_record_dev, s_config.mic_gain_db);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "set mic gain failed: %d", ret);
    }

    ESP_LOGI(TAG, "ES7210 microphones initialized: %lu Hz, %u channels, %u bits, gain %.1f dB",
             s_config.sample_rate_hz, s_config.channel_count, s_config.bits_per_sample, s_config.mic_gain_db);
    return ESP_OK;
}

esp_err_t board_mic_read(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms)
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

esp_err_t board_mic_set_gain(float gain_db)
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

esp_err_t board_mic_self_test(void)
{
    esp_err_t ret = board_mic_init(NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    const size_t frame_count = 1024;
    const size_t sample_count = frame_count * s_config.channel_count;
    int16_t *pcm = (int16_t *)heap_caps_calloc(sample_count, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = 0;
    ret = board_mic_read(pcm, sample_count * sizeof(int16_t), &bytes_read, BOARD_MIC_READ_TIMEOUT_MS);
    if (ret != ESP_OK) {
        free(pcm);
        return ret;
    }

    int16_t ch0_peak = 0;
    int16_t ch1_peak = 0;
    int64_t ch0_sum = 0;
    int64_t ch1_sum = 0;
    size_t frames_read = bytes_read / (sizeof(int16_t) * s_config.channel_count);

    for (size_t i = 0; i < frames_read; i++) {
        int16_t left = pcm[i * 2];
        int16_t right = pcm[i * 2 + 1];
        int16_t left_abs = left < 0 ? -left : left;
        int16_t right_abs = right < 0 ? -right : right;

        if (left_abs > ch0_peak) {
            ch0_peak = left_abs;
        }
        if (right_abs > ch1_peak) {
            ch1_peak = right_abs;
        }
        ch0_sum += left_abs;
        ch1_sum += right_abs;
    }

    ESP_LOGI(TAG, "Mic self-test read %u bytes, frames=%u, CH0 peak=%d avg=%lld, CH1 peak=%d avg=%lld",
             (unsigned)bytes_read, (unsigned)frames_read,
             ch0_peak, frames_read ? ch0_sum / (int64_t)frames_read : 0,
             ch1_peak, frames_read ? ch1_sum / (int64_t)frames_read : 0);

    free(pcm);
    return ESP_OK;
}

void board_mic_deinit(void)
{
    if (s_record_dev != NULL) {
        esp_codec_dev_close(s_record_dev);
        esp_codec_dev_delete(s_record_dev);
        s_record_dev = NULL;
    }
    if (s_codec_if != NULL) {
        audio_codec_delete_codec_if(s_codec_if);
        s_codec_if = NULL;
    }
    if (s_ctrl_if != NULL) {
        audio_codec_delete_ctrl_if(s_ctrl_if);
        s_ctrl_if = NULL;
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
