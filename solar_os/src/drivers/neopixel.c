#include "neopixel.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"

#define NEOPIXEL_RMT_RESOLUTION_HZ 10000000U
#define NEOPIXEL_RESET_US 80U
#define NEOPIXEL_WRITE_TIMEOUT_MS 1000

static const char *TAG = "neopixel";

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t copy_encoder;
    rmt_symbol_word_t reset_code;
    int state;
} neopixel_encoder_t;

RMT_ENCODER_FUNC_ATTR
static size_t neopixel_encode(rmt_encoder_t *encoder,
                              rmt_channel_handle_t channel,
                              const void *primary_data,
                              size_t data_size,
                              rmt_encode_state_t *ret_state)
{
    neopixel_encoder_t *pixel_encoder = __containerof(encoder, neopixel_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (pixel_encoder->state) {
    case 0:
        encoded_symbols += pixel_encoder->bytes_encoder->encode(pixel_encoder->bytes_encoder,
                                                                 channel,
                                                                 primary_data,
                                                                 data_size,
                                                                 &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            pixel_encoder->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            break;
        }
        /* Fall through to append the reset pulse. */
    case 1:
        encoded_symbols += pixel_encoder->copy_encoder->encode(pixel_encoder->copy_encoder,
                                                                channel,
                                                                &pixel_encoder->reset_code,
                                                                sizeof(pixel_encoder->reset_code),
                                                                &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            pixel_encoder->state = 0;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
        }
        break;
    default:
        pixel_encoder->state = 0;
        state = RMT_ENCODING_COMPLETE;
        break;
    }

    *ret_state = state;
    return encoded_symbols;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t neopixel_encoder_reset(rmt_encoder_t *encoder)
{
    neopixel_encoder_t *pixel_encoder = __containerof(encoder, neopixel_encoder_t, base);
    ESP_RETURN_ON_ERROR(rmt_encoder_reset(pixel_encoder->bytes_encoder), TAG, "reset bytes encoder");
    ESP_RETURN_ON_ERROR(rmt_encoder_reset(pixel_encoder->copy_encoder), TAG, "reset copy encoder");
    pixel_encoder->state = 0;
    return ESP_OK;
}

static esp_err_t neopixel_encoder_delete(rmt_encoder_t *encoder)
{
    neopixel_encoder_t *pixel_encoder = __containerof(encoder, neopixel_encoder_t, base);
    if (pixel_encoder->bytes_encoder != NULL) {
        (void)rmt_del_encoder(pixel_encoder->bytes_encoder);
    }
    if (pixel_encoder->copy_encoder != NULL) {
        (void)rmt_del_encoder(pixel_encoder->copy_encoder);
    }
    free(pixel_encoder);
    return ESP_OK;
}

static esp_err_t neopixel_new_encoder(rmt_encoder_handle_t *encoder)
{
    if (encoder == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    neopixel_encoder_t *pixel_encoder = rmt_alloc_encoder_mem(sizeof(*pixel_encoder));
    if (pixel_encoder == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(pixel_encoder, 0, sizeof(*pixel_encoder));
    pixel_encoder->base.encode = neopixel_encode;
    pixel_encoder->base.reset = neopixel_encoder_reset;
    pixel_encoder->base.del = neopixel_encoder_delete;

    const rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 3,
            .level1 = 0,
            .duration1 = 9,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 9,
            .level1 = 0,
            .duration1 = 3,
        },
        .flags.msb_first = 1,
    };
    esp_err_t ret = rmt_new_bytes_encoder(&bytes_config, &pixel_encoder->bytes_encoder);
    if (ret != ESP_OK) {
        neopixel_encoder_delete(&pixel_encoder->base);
        return ret;
    }

    const rmt_copy_encoder_config_t copy_config = {};
    ret = rmt_new_copy_encoder(&copy_config, &pixel_encoder->copy_encoder);
    if (ret != ESP_OK) {
        neopixel_encoder_delete(&pixel_encoder->base);
        return ret;
    }

    const uint32_t reset_ticks =
        (NEOPIXEL_RMT_RESOLUTION_HZ / 1000000U) * NEOPIXEL_RESET_US / 2U;
    pixel_encoder->reset_code = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = reset_ticks,
        .level1 = 0,
        .duration1 = reset_ticks,
    };
    *encoder = &pixel_encoder->base;
    return ESP_OK;
}

esp_err_t neopixel_init(neopixel_t *strip, int data_pin)
{
    if (strip == NULL || !GPIO_IS_VALID_OUTPUT_GPIO(data_pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(strip, 0, sizeof(*strip));
    strip->data_pin = data_pin;
    const rmt_tx_channel_config_t channel_config = {
        .gpio_num = data_pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = NEOPIXEL_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
        .flags.init_level = 0,
    };
    esp_err_t ret = rmt_new_tx_channel(&channel_config, &strip->channel);
    if (ret != ESP_OK) {
        neopixel_deinit(strip);
        return ret;
    }
    ret = neopixel_new_encoder(&strip->encoder);
    if (ret != ESP_OK) {
        neopixel_deinit(strip);
        return ret;
    }
    ret = rmt_enable(strip->channel);
    if (ret != ESP_OK) {
        neopixel_deinit(strip);
        return ret;
    }
    strip->enabled = true;
    return ESP_OK;
}

esp_err_t neopixel_write(neopixel_t *strip, const uint8_t *grb, size_t byte_count)
{
    if (strip == NULL || !strip->enabled || strip->channel == NULL ||
        strip->encoder == NULL || grb == NULL || byte_count == 0 ||
        (byte_count % 3U) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    ESP_RETURN_ON_ERROR(rmt_transmit(strip->channel,
                                     strip->encoder,
                                     grb,
                                     byte_count,
                                     &transmit_config),
                        TAG,
                        "transmit failed");
    return rmt_tx_wait_all_done(strip->channel, NEOPIXEL_WRITE_TIMEOUT_MS);
}

void neopixel_deinit(neopixel_t *strip)
{
    if (strip == NULL) {
        return;
    }
    if (strip->enabled && strip->channel != NULL) {
        (void)rmt_disable(strip->channel);
    }
    if (strip->encoder != NULL) {
        (void)rmt_del_encoder(strip->encoder);
    }
    if (strip->channel != NULL) {
        (void)rmt_del_channel(strip->channel);
    }
    if (GPIO_IS_VALID_GPIO(strip->data_pin)) {
        (void)gpio_reset_pin((gpio_num_t)strip->data_pin);
    }
    memset(strip, 0, sizeof(*strip));
    strip->data_pin = -1;
}
