#include "board_rlcd.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_rlcd";

/*
 * 官方示例中的屏幕连接关系：
 *
 *   DisplayPort RlcdPort(12, 11, 5, 40, 41, 400, 300);
 *
 * 对应含义：
 *   MOSI = GPIO12
 *   SCK  = GPIO11
 *   DC   = GPIO5
 *   CS   = GPIO40
 *   RST  = GPIO41
 */
#define BOARD_RLCD_PIN_MOSI  GPIO_NUM_12
#define BOARD_RLCD_PIN_SCK   GPIO_NUM_11
#define BOARD_RLCD_PIN_DC    GPIO_NUM_5
#define BOARD_RLCD_PIN_CS    GPIO_NUM_40
#define BOARD_RLCD_PIN_RST   GPIO_NUM_41

#define BOARD_RLCD_SPI_HOST  SPI3_HOST
#define BOARD_RLCD_SPI_CLOCK_HZ  (40 * 1000 * 1000)

/*
 * 这块屏幕是 1 bit 黑白屏。
 *
 * 400 * 300 = 120000 个像素。
 * 每 8 个像素打包成 1 字节。
 * 所以显存大小是 120000 / 8 = 15000 字节。
 */
#define BOARD_RLCD_BUFFER_SIZE  ((BOARD_RLCD_WIDTH * BOARD_RLCD_HEIGHT) / 8)
#define BOARD_RLCD_BYTE_COLUMNS (BOARD_RLCD_WIDTH / 2)
#define BOARD_RLCD_BYTE_ROWS    (BOARD_RLCD_HEIGHT / 4)
#define BOARD_RLCD_ROW_GROUPS   (BOARD_RLCD_BYTE_ROWS / 3)
#define BOARD_RLCD_COL_OFFSET   0x12

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static uint8_t *s_frame_buffer = NULL;
static uint8_t *s_transfer_buffer = NULL;
static bool s_initialized = false;
static board_rlcd_rotation_t s_rotation = BOARD_RLCD_ROTATION_0;
static bool s_force_full_flush = false;

static void board_rlcd_set_reset_level(uint8_t level) {
    gpio_set_level(BOARD_RLCD_PIN_RST, level ? 1 : 0);
}

// 发送指令
static esp_err_t board_rlcd_send_command(uint8_t command) {
    return esp_lcd_panel_io_tx_param(s_io_handle, command, NULL, 0);
}

// 发送参数
static esp_err_t board_rlcd_send_data(uint8_t data) {
    return esp_lcd_panel_io_tx_param(s_io_handle, -1, &data, 1);
}

// 发送像素数据
static esp_err_t board_rlcd_send_buffer(const uint8_t *data, size_t len) {
    return esp_lcd_panel_io_tx_color(s_io_handle, -1, data, len);
}

typedef struct {
    uint8_t command;
    uint8_t data_len;
    uint8_t data[10];
    uint16_t delay_ms;
} board_rlcd_init_cmd_t;

static esp_err_t board_rlcd_send_command_list(const board_rlcd_init_cmd_t *commands, size_t command_count) {
    for (size_t i = 0; i < command_count; i++) {
        ESP_RETURN_ON_ERROR(board_rlcd_send_command(commands[i].command), TAG, "send command failed");

        for (uint8_t data_index = 0; data_index < commands[i].data_len; data_index++) {
            ESP_RETURN_ON_ERROR(board_rlcd_send_data(commands[i].data[data_index]), TAG, "send data failed");
        }

        if (commands[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(commands[i].delay_ms));
        }
    }

    return ESP_OK;
}

static esp_err_t board_rlcd_set_address_window(uint8_t row_group_start,
                                               uint8_t row_group_end,
                                               uint8_t byte_x_start,
                                               uint8_t byte_x_end) {
    ESP_RETURN_ON_ERROR(board_rlcd_send_command(0x2A), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(board_rlcd_send_data((uint8_t)(BOARD_RLCD_COL_OFFSET + row_group_start)), TAG,
                        "send data failed");
    ESP_RETURN_ON_ERROR(board_rlcd_send_data((uint8_t)(BOARD_RLCD_COL_OFFSET + row_group_end)), TAG,
                        "send data failed");

    ESP_RETURN_ON_ERROR(board_rlcd_send_command(0x2B), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(board_rlcd_send_data(byte_x_start), TAG, "send data failed");
    ESP_RETURN_ON_ERROR(board_rlcd_send_data(byte_x_end), TAG, "send data failed");

    return board_rlcd_send_command(0x2C);
}

static void board_rlcd_reset(void) {
    board_rlcd_set_reset_level(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    board_rlcd_set_reset_level(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    board_rlcd_set_reset_level(1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void board_rlcd_get_landscape_pixel_location(uint16_t x, uint16_t y, uint32_t *index, uint8_t *mask) {
    /*
     * 这里沿用官方示例的横屏坐标映射。
     *
     * 用户看到的坐标：
     *   x: 0..399
     *   y: 0..299
     *
     * 屏幕控制器要求的 1 bit 数据排列并不是简单的 x/y 顺序。
     * 这里把 (x, y) 映射到：
     *   1. 显存中的第几个字节；
     *   2. 这个字节里的第几个 bit。
     *
     * 官方示例为了速度使用了查表法。
     * 当前驱动继续使用现算方式，因为这个换算只涉及移位和加法，
     * 开销很小，而且可以避免为坐标查表额外占用一大块内存。
     */

    // 获取当前屏幕有多少个像素块
    const uint16_t height_div_4 = BOARD_RLCD_HEIGHT >> 2;

    // 获取当前像素的Y坐标
    uint16_t inv_y = BOARD_RLCD_HEIGHT - 1 - y;

    // 获取当前像素按照像素块排列Y轴排在第几
    uint16_t block_y = inv_y >> 2;

    // 获取当前像素在当前像素块中排在第几列
    uint8_t local_y = inv_y & 3;

    // 获取当前像素按照像素块排在X轴的第几个像素块中
    uint16_t byte_x = x >> 1;

    // 获取当前像素在像素块中x轴坐标
    uint8_t local_x = x & 1;

    // 将像素块中的坐标映射到字节的bit中
    uint8_t bit = 7 - ((local_y << 1) | local_x);

    // 获取当前像素在整个缓冲区的序列
    *index = byte_x * height_div_4 + block_y;

    // 获取像素在字节中的位掩码
    *mask = (uint8_t) (1U << bit);
}

static bool board_rlcd_is_valid_rotation(board_rlcd_rotation_t rotation)
{
    return rotation == BOARD_RLCD_ROTATION_0 ||
           rotation == BOARD_RLCD_ROTATION_90 ||
           rotation == BOARD_RLCD_ROTATION_180 ||
           rotation == BOARD_RLCD_ROTATION_270;
}

static void board_rlcd_transform_logical_to_physical(uint16_t logical_x,
                                                     uint16_t logical_y,
                                                     uint16_t *physical_x,
                                                     uint16_t *physical_y)
{
    switch (s_rotation) {
    case BOARD_RLCD_ROTATION_0:
        *physical_x = logical_x;
        *physical_y = logical_y;
        break;

    case BOARD_RLCD_ROTATION_90:
        *physical_x = (uint16_t)(BOARD_RLCD_WIDTH - 1 - logical_y);
        *physical_y = logical_x;
        break;

    case BOARD_RLCD_ROTATION_180:
        *physical_x = (uint16_t)(BOARD_RLCD_WIDTH - 1 - logical_x);
        *physical_y = (uint16_t)(BOARD_RLCD_HEIGHT - 1 - logical_y);
        break;

    case BOARD_RLCD_ROTATION_270:
        *physical_x = logical_y;
        *physical_y = (uint16_t)(BOARD_RLCD_HEIGHT - 1 - logical_x);
        break;

    default:
        *physical_x = logical_x;
        *physical_y = logical_y;
        break;
    }
}

static esp_err_t board_rlcd_get_physical_area(uint16_t x1,
                                              uint16_t y1,
                                              uint16_t x2,
                                              uint16_t y2,
                                              uint16_t *out_x1,
                                              uint16_t *out_y1,
                                              uint16_t *out_x2,
                                              uint16_t *out_y2)
{
    if (x1 > x2 || y1 > y2 || x2 >= board_rlcd_get_logical_width() || y2 >= board_rlcd_get_logical_height()) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t px[4];
    uint16_t py[4];

    board_rlcd_transform_logical_to_physical(x1, y1, &px[0], &py[0]);
    board_rlcd_transform_logical_to_physical(x2, y1, &px[1], &py[1]);
    board_rlcd_transform_logical_to_physical(x1, y2, &px[2], &py[2]);
    board_rlcd_transform_logical_to_physical(x2, y2, &px[3], &py[3]);

    uint16_t min_x = px[0];
    uint16_t max_x = px[0];
    uint16_t min_y = py[0];
    uint16_t max_y = py[0];

    for (size_t i = 1; i < 4; i++) {
        if (px[i] < min_x) {
            min_x = px[i];
        }
        if (px[i] > max_x) {
            max_x = px[i];
        }
        if (py[i] < min_y) {
            min_y = py[i];
        }
        if (py[i] > max_y) {
            max_y = py[i];
        }
    }

    *out_x1 = min_x;
    *out_y1 = min_y;
    *out_x2 = max_x;
    *out_y2 = max_y;
    return ESP_OK;
}

static esp_err_t board_rlcd_set_physical_pixel(uint16_t x, uint16_t y, board_rlcd_color_t color)
{
    if (x >= BOARD_RLCD_WIDTH || y >= BOARD_RLCD_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t index = 0;
    uint8_t mask = 0;
    board_rlcd_get_landscape_pixel_location(x, y, &index, &mask);

    if (color == BOARD_RLCD_COLOR_WHITE) {
        s_frame_buffer[index] |= mask;
    } else {
        s_frame_buffer[index] &= (uint8_t) ~mask;
    }

    return ESP_OK;
}

static esp_err_t board_rlcd_send_init_sequence(void) {
    /*
     * 以下初始化命令来自官方 08_LVGL_V8_Test 的 display_bsp.cpp。
     *
     * 这部分不是 ESP32-S3 的通用逻辑，而是这块 4.2 寸 RLCD 屏幕控制器
     * 自己要求的寄存器配置。当前阶段先保持和官方示例一致。
     */
    static const board_rlcd_init_cmd_t init_commands[] = {
        {0xD6, 2, {0x17, 0x02}, 0},
        {0xD1, 1, {0x01}, 0},
        {0xC0, 2, {0x11, 0x04}, 0},
        {0xC1, 4, {0x69, 0x69, 0x69, 0x69}, 0},
        {0xC2, 4, {0x19, 0x19, 0x19, 0x19}, 0},
        {0xC4, 4, {0x4B, 0x4B, 0x4B, 0x4B}, 0},
        {0xC5, 4, {0x19, 0x19, 0x19, 0x19}, 0},
        {0xD8, 2, {0x80, 0xE9}, 0},
        {0xB2, 1, {0x02}, 0},
        {0xB3, 10, {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 0},
        {0xB4, 8, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 0},
        {0x62, 3, {0x32, 0x03, 0x1F}, 0},
        {0xB7, 1, {0x13}, 0},
        {0xB0, 1, {0x64}, 0},
        {0x11, 0, {}, 200},
        {0xC9, 1, {0x00}, 0},
        {0x36, 1, {0x48}, 0},
        {0x3A, 1, {0x11}, 0},
        {0xB9, 1, {0x20}, 0},
        {0xB8, 1, {0x29}, 0},
        {0x21, 0, {}, 0},
        {0x2A, 2, {0x12, 0x2A}, 0},
        {0x2B, 2, {0x00, 0xC7}, 0},
        {0x35, 1, {0x00}, 0},
        {0xD0, 1, {0xFF}, 0},
        {0x38, 0, {}, 0},
        {0x29, 0, {}, 0},
    };

    return board_rlcd_send_command_list(init_commands, sizeof(init_commands) / sizeof(init_commands[0]));
}

esp_err_t board_rlcd_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing RLCD: MOSI=%d SCK=%d DC=%d CS=%d RST=%d, size=%dx%d",
             BOARD_RLCD_PIN_MOSI,
             BOARD_RLCD_PIN_SCK,
             BOARD_RLCD_PIN_DC,
             BOARD_RLCD_PIN_CS,
             BOARD_RLCD_PIN_RST,
             BOARD_RLCD_WIDTH,
             BOARD_RLCD_HEIGHT);

    // 配置SPI
    spi_bus_config_t bus_config = {};
    bus_config.miso_io_num = -1;
    bus_config.mosi_io_num = BOARD_RLCD_PIN_MOSI;
    bus_config.sclk_io_num = BOARD_RLCD_PIN_SCK;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = BOARD_RLCD_BUFFER_SIZE;

    // 初始化SPI
    esp_err_t ret = spi_bus_initialize(BOARD_RLCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置SPI的IO平面
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = BOARD_RLCD_PIN_DC;
    io_config.cs_gpio_num = BOARD_RLCD_PIN_CS;
    io_config.pclk_hz = BOARD_RLCD_SPI_CLOCK_HZ;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;

    ESP_RETURN_ON_ERROR(
        // 创建IO平面
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_RLCD_SPI_HOST, &io_config, &s_io_handle),
        TAG,
        "Failed to create LCD SPI IO"
    );

    gpio_config_t reset_gpio_config = {};
    reset_gpio_config.intr_type = GPIO_INTR_DISABLE;
    reset_gpio_config.mode = GPIO_MODE_OUTPUT;
    reset_gpio_config.pin_bit_mask = (1ULL << BOARD_RLCD_PIN_RST);
    reset_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    reset_gpio_config.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&reset_gpio_config), TAG, "Failed to configure reset GPIO");

    /*
     * RLCD framebuffer 只有 15KB，且 board_rlcd_set_pixel() 会频繁随机读写它。
     * 优先放内部 RAM 会比 PSRAM 更快；内部 RAM 不够时再退到 PSRAM。
     */
    s_frame_buffer = (uint8_t *) heap_caps_malloc(BOARD_RLCD_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_frame_buffer == NULL) {
        s_frame_buffer = (uint8_t *) heap_caps_malloc(BOARD_RLCD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_frame_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate RLCD frame buffer");
        return ESP_ERR_NO_MEM;
    }

    /*
     * transfer buffer 会直接交给 LCD SPI IO 发送。优先使用内部 DMA 内存，
     * 可以减少驱动内部为了 DMA 兼容而做额外拷贝的可能性。
     */
    s_transfer_buffer = (uint8_t *) heap_caps_malloc(
        BOARD_RLCD_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_transfer_buffer == NULL) {
        s_transfer_buffer = (uint8_t *) heap_caps_malloc(BOARD_RLCD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_transfer_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate RLCD transfer buffer");
        return ESP_ERR_NO_MEM;
    }

    board_rlcd_reset();
    ESP_RETURN_ON_ERROR(board_rlcd_send_init_sequence(), TAG, "Failed to send RLCD init sequence");

    // 将申请下来的内存全部填充为0x00
    memset(s_frame_buffer, BOARD_RLCD_COLOR_BLACK, BOARD_RLCD_BUFFER_SIZE);
    s_rotation = BOARD_RLCD_ROTATION_0;
    s_force_full_flush = false;
    s_initialized = true;

    ESP_LOGI(TAG, "RLCD initialized");
    return ESP_OK;
}

bool board_rlcd_is_initialized(void) {
    return s_initialized;
}

esp_err_t board_rlcd_set_rotation(board_rlcd_rotation_t rotation)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!board_rlcd_is_valid_rotation(rotation)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_rotation != rotation) {
        s_rotation = rotation;
        s_force_full_flush = true;
    }

    return ESP_OK;
}

board_rlcd_rotation_t board_rlcd_get_rotation(void)
{
    return s_rotation;
}

uint16_t board_rlcd_get_logical_width(void)
{
    return (s_rotation == BOARD_RLCD_ROTATION_90 || s_rotation == BOARD_RLCD_ROTATION_270)
               ? BOARD_RLCD_HEIGHT
               : BOARD_RLCD_WIDTH;
}

uint16_t board_rlcd_get_logical_height(void)
{
    return (s_rotation == BOARD_RLCD_ROTATION_90 || s_rotation == BOARD_RLCD_ROTATION_270)
               ? BOARD_RLCD_WIDTH
               : BOARD_RLCD_HEIGHT;
}

esp_err_t board_rlcd_clear(board_rlcd_color_t color) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_frame_buffer, (uint8_t) color, BOARD_RLCD_BUFFER_SIZE);
    s_force_full_flush = true;
    return ESP_OK;
}

esp_err_t board_rlcd_set_pixel(uint16_t x, uint16_t y, board_rlcd_color_t color) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (x >= board_rlcd_get_logical_width() || y >= board_rlcd_get_logical_height()) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t physical_x = 0;
    uint16_t physical_y = 0;
    board_rlcd_transform_logical_to_physical(x, y, &physical_x, &physical_y);

    return board_rlcd_set_physical_pixel(physical_x, physical_y, color);
}

static void board_rlcd_get_gb_2x2_dither(uint8_t shade, board_rlcd_color_t out[4])
{
    switch (shade & 0x03U) {
    case 0:
        out[0] = BOARD_RLCD_COLOR_WHITE;
        out[1] = BOARD_RLCD_COLOR_WHITE;
        out[2] = BOARD_RLCD_COLOR_WHITE;
        out[3] = BOARD_RLCD_COLOR_WHITE;
        break;
    case 1:
        out[0] = BOARD_RLCD_COLOR_BLACK;
        out[1] = BOARD_RLCD_COLOR_WHITE;
        out[2] = BOARD_RLCD_COLOR_WHITE;
        out[3] = BOARD_RLCD_COLOR_WHITE;
        break;
    case 2:
        out[0] = BOARD_RLCD_COLOR_BLACK;
        out[1] = BOARD_RLCD_COLOR_WHITE;
        out[2] = BOARD_RLCD_COLOR_WHITE;
        out[3] = BOARD_RLCD_COLOR_BLACK;
        break;
    case 3:
    default:
        out[0] = BOARD_RLCD_COLOR_BLACK;
        out[1] = BOARD_RLCD_COLOR_BLACK;
        out[2] = BOARD_RLCD_COLOR_BLACK;
        out[3] = BOARD_RLCD_COLOR_BLACK;
        break;
    }
}

static void board_rlcd_write_masked_pixel(uint32_t index, uint8_t mask, board_rlcd_color_t color)
{
    if (color == BOARD_RLCD_COLOR_WHITE) {
        s_frame_buffer[index] |= mask;
    } else {
        s_frame_buffer[index] &= (uint8_t)~mask;
    }
}

static uint8_t board_rlcd_rgb565_be_to_gbc_shade(uint16_t rgb565_be)
{
    uint16_t rgb = (uint16_t)((rgb565_be >> 8) | (rgb565_be << 8));
    uint8_t r5 = (uint8_t)((rgb >> 11) & 0x1FU);
    uint8_t g6 = (uint8_t)((rgb >> 5) & 0x3FU);
    uint8_t b5 = (uint8_t)(rgb & 0x1FU);
    uint16_t gray = (uint16_t)(r5 * 38U + g6 * 75U + b5 * 15U);

    if (gray >= 4800U) {
        return 0;
    }
    if (gray >= 3200U) {
        return 1;
    }
    if (gray >= 1600U) {
        return 2;
    }
    return 3;
}

static void board_rlcd_write_gbc_dither(uint32_t index0,
                                        uint8_t left_mask0,
                                        uint8_t right_mask0,
                                        uint32_t index1,
                                        uint8_t left_mask1,
                                        uint8_t right_mask1,
                                        uint8_t shade)
{
    uint8_t row0_white = (uint8_t)(left_mask0 | right_mask0);
    uint8_t row1_white = (uint8_t)(left_mask1 | right_mask1);
    uint8_t row0_black = 0;
    uint8_t row1_black = 0;

    switch (shade & 0x03U) {
    case 0:
        break;
    case 1:
        row0_black = left_mask0;
        break;
    case 2:
        row0_black = left_mask0;
        row1_black = right_mask1;
        break;
    case 3:
    default:
        row0_black = row0_white;
        row1_black = row1_white;
        break;
    }

    row0_white = (uint8_t)(row0_white & ~row0_black);
    row1_white = (uint8_t)(row1_white & ~row1_black);

    s_frame_buffer[index0] = (uint8_t)((s_frame_buffer[index0] | row0_white) & ~row0_black);
    s_frame_buffer[index1] = (uint8_t)((s_frame_buffer[index1] | row1_white) & ~row1_black);
}

esp_err_t board_rlcd_draw_gb_line_2x(uint16_t dst_x, uint16_t dst_y, const uint8_t *pixels, uint16_t pixel_count)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (pixels == NULL || dst_x + pixel_count * 2 > board_rlcd_get_logical_width() ||
        dst_y + 1 >= board_rlcd_get_logical_height()) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_rotation != BOARD_RLCD_ROTATION_0 || (dst_x & 1U) != 0) {
        for (uint16_t x = 0; x < pixel_count; x++) {
            board_rlcd_color_t dither[4];
            board_rlcd_get_gb_2x2_dither(pixels[x], dither);
            uint16_t out_x = (uint16_t)(dst_x + x * 2);
            (void)board_rlcd_set_pixel(out_x, dst_y, dither[0]);
            (void)board_rlcd_set_pixel(out_x + 1, dst_y, dither[1]);
            (void)board_rlcd_set_pixel(out_x, dst_y + 1, dither[2]);
            (void)board_rlcd_set_pixel(out_x + 1, dst_y + 1, dither[3]);
        }

        return ESP_OK;
    }

    const uint16_t height_div_4 = BOARD_RLCD_HEIGHT >> 2;
    const uint16_t start_byte_x = dst_x >> 1;

    uint16_t inv_y0 = (uint16_t)(BOARD_RLCD_HEIGHT - 1 - dst_y);
    uint16_t block_y0 = inv_y0 >> 2;
    uint8_t local_y0 = inv_y0 & 3U;
    uint8_t bit0 = (uint8_t)(7U - (local_y0 << 1));
    uint8_t left_mask0 = (uint8_t)(1U << bit0);
    uint8_t right_mask0 = (uint8_t)(1U << (bit0 - 1U));

    uint16_t inv_y1 = (uint16_t)(BOARD_RLCD_HEIGHT - 1 - (dst_y + 1));
    uint16_t block_y1 = inv_y1 >> 2;
    uint8_t local_y1 = inv_y1 & 3U;
    uint8_t bit1 = (uint8_t)(7U - (local_y1 << 1));
    uint8_t left_mask1 = (uint8_t)(1U << bit1);
    uint8_t right_mask1 = (uint8_t)(1U << (bit1 - 1U));

    for (uint16_t x = 0; x < pixel_count; x++) {
        uint32_t index0 = (uint32_t)(start_byte_x + x) * height_div_4 + block_y0;
        uint32_t index1 = (uint32_t)(start_byte_x + x) * height_div_4 + block_y1;
        board_rlcd_color_t dither[4];
        board_rlcd_get_gb_2x2_dither(pixels[x], dither);

        board_rlcd_write_masked_pixel(index0, left_mask0, dither[0]);
        board_rlcd_write_masked_pixel(index0, right_mask0, dither[1]);
        board_rlcd_write_masked_pixel(index1, left_mask1, dither[2]);
        board_rlcd_write_masked_pixel(index1, right_mask1, dither[3]);
    }

    return ESP_OK;
}

esp_err_t board_rlcd_draw_gbc_line_2x_rgb565_be(uint16_t dst_x,
                                                uint16_t dst_y,
                                                const uint16_t *pixels,
                                                uint16_t pixel_count)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (pixels == NULL || dst_x + pixel_count * 2 > board_rlcd_get_logical_width() ||
        dst_y + 1 >= board_rlcd_get_logical_height()) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_rotation != BOARD_RLCD_ROTATION_0 || (dst_x & 1U) != 0) {
        for (uint16_t x = 0; x < pixel_count; x++) {
            board_rlcd_color_t dither[4];
            board_rlcd_get_gb_2x2_dither(board_rlcd_rgb565_be_to_gbc_shade(pixels[x]), dither);
            uint16_t out_x = (uint16_t)(dst_x + x * 2);
            (void)board_rlcd_set_pixel(out_x, dst_y, dither[0]);
            (void)board_rlcd_set_pixel(out_x + 1, dst_y, dither[1]);
            (void)board_rlcd_set_pixel(out_x, dst_y + 1, dither[2]);
            (void)board_rlcd_set_pixel(out_x + 1, dst_y + 1, dither[3]);
        }

        return ESP_OK;
    }

    const uint16_t height_div_4 = BOARD_RLCD_HEIGHT >> 2;
    const uint16_t start_byte_x = dst_x >> 1;

    uint16_t inv_y0 = (uint16_t)(BOARD_RLCD_HEIGHT - 1 - dst_y);
    uint16_t block_y0 = inv_y0 >> 2;
    uint8_t local_y0 = inv_y0 & 3U;
    uint8_t bit0 = (uint8_t)(7U - (local_y0 << 1));
    uint8_t left_mask0 = (uint8_t)(1U << bit0);
    uint8_t right_mask0 = (uint8_t)(1U << (bit0 - 1U));

    uint16_t inv_y1 = (uint16_t)(BOARD_RLCD_HEIGHT - 1 - (dst_y + 1));
    uint16_t block_y1 = inv_y1 >> 2;
    uint8_t local_y1 = inv_y1 & 3U;
    uint8_t bit1 = (uint8_t)(7U - (local_y1 << 1));
    uint8_t left_mask1 = (uint8_t)(1U << bit1);
    uint8_t right_mask1 = (uint8_t)(1U << (bit1 - 1U));

    for (uint16_t x = 0; x < pixel_count; x++) {
        uint32_t index0 = (uint32_t)(start_byte_x + x) * height_div_4 + block_y0;
        uint32_t index1 = (uint32_t)(start_byte_x + x) * height_div_4 + block_y1;
        uint8_t shade = board_rlcd_rgb565_be_to_gbc_shade(pixels[x]);

        board_rlcd_write_gbc_dither(index0, left_mask0, right_mask0, index1, left_mask1, right_mask1, shade);
    }

    return ESP_OK;
}

esp_err_t board_rlcd_flush(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(board_rlcd_set_address_window(0, BOARD_RLCD_ROW_GROUPS - 1, 0, BOARD_RLCD_BYTE_COLUMNS - 1),
                        TAG,
                        "set full address window failed");
    ESP_RETURN_ON_ERROR(board_rlcd_send_buffer(s_frame_buffer, BOARD_RLCD_BUFFER_SIZE), TAG,
                        "send frame buffer failed");

    s_force_full_flush = false;
    return ESP_OK;
}

esp_err_t board_rlcd_refresh(void)
{
    return board_rlcd_flush();
}

static esp_err_t board_rlcd_flush_physical_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (x1 > x2 || y1 > y2 || x2 >= BOARD_RLCD_WIDTH || y2 >= BOARD_RLCD_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * 横屏模式下，一个显存字节覆盖 2 个横向像素、4 个纵向像素。
     * 控制器的 0x2A 地址再把 3 个这样的纵向块合成一组，所以局部刷新
     * 需要扩展到 2 x 12 像素边界，才能保证发给控制器的数据连续。
     */
    const uint16_t byte_x_start = x1 >> 1;
    const uint16_t byte_x_end = x2 >> 1;

    const uint16_t block_y_start = (BOARD_RLCD_HEIGHT - 1 - y2) >> 2;
    const uint16_t block_y_end = (BOARD_RLCD_HEIGHT - 1 - y1) >> 2;
    const uint16_t row_group_start = block_y_start / 3;
    const uint16_t row_group_end = block_y_end / 3;

    const size_t byte_x_count = byte_x_end - byte_x_start + 1;
    const size_t row_group_count = row_group_end - row_group_start + 1;
    const size_t transfer_len = byte_x_count * row_group_count * 3;

    size_t out = 0;
    for (uint16_t byte_x = byte_x_start; byte_x <= byte_x_end; byte_x++) {
        for (uint16_t row_group = row_group_start; row_group <= row_group_end; row_group++) {
            uint16_t block_y = row_group * 3;
            s_transfer_buffer[out++] = s_frame_buffer[byte_x * BOARD_RLCD_BYTE_ROWS + block_y];
            s_transfer_buffer[out++] = s_frame_buffer[byte_x * BOARD_RLCD_BYTE_ROWS + block_y + 1];
            s_transfer_buffer[out++] = s_frame_buffer[byte_x * BOARD_RLCD_BYTE_ROWS + block_y + 2];
        }
    }

    ESP_RETURN_ON_ERROR(board_rlcd_set_address_window((uint8_t)row_group_start,
                            (uint8_t)row_group_end,
                            (uint8_t)byte_x_start,
                            (uint8_t)byte_x_end),
                        TAG,
                        "set area address window failed");
    ESP_RETURN_ON_ERROR(board_rlcd_send_buffer(s_transfer_buffer, transfer_len), TAG, "send area buffer failed");

    return ESP_OK;
}

esp_err_t board_rlcd_flush_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_force_full_flush) {
        return board_rlcd_flush();
    }

    uint16_t physical_x1 = 0;
    uint16_t physical_y1 = 0;
    uint16_t physical_x2 = 0;
    uint16_t physical_y2 = 0;

    ESP_RETURN_ON_ERROR(board_rlcd_get_physical_area(x1,
                                                     y1,
                                                     x2,
                                                     y2,
                                                     &physical_x1,
                                                     &physical_y1,
                                                     &physical_x2,
                                                     &physical_y2),
                        TAG,
                        "get physical area failed");

    return board_rlcd_flush_physical_area(physical_x1, physical_y1, physical_x2, physical_y2);
}

esp_err_t board_rlcd_refresh_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    return board_rlcd_flush_area(x1, y1, x2, y2);
}

esp_err_t board_rlcd_flush_rgb565_area(uint16_t x1,
                                       uint16_t y1,
                                       uint16_t x2,
                                       uint16_t y2,
                                       const uint16_t *pixels)
{
    if (pixels == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (x1 > x2 || y1 > y2 || x2 >= board_rlcd_get_logical_width() || y2 >= board_rlcd_get_logical_height()) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t *src = pixels;
    for (uint16_t y = y1; y <= y2; y++) {
        for (uint16_t x = x1; x <= x2; x++) {
            uint16_t rgb565 = *src++;
            board_rlcd_color_t color = (rgb565 < 0x7fff) ? BOARD_RLCD_COLOR_BLACK : BOARD_RLCD_COLOR_WHITE;
            ESP_RETURN_ON_ERROR(board_rlcd_set_pixel(x, y, color), TAG, "set pixel failed");
        }
    }

    /*
     * 临时使用整屏 flush 验证 LVGL 旋转和响应式布局。
     *
     * 当前 ST7305 的局部刷新打包顺序还没有完全校准，横屏下会出现旧区域
     * 和新区域混在一起。整屏 flush 虽然慢一些，但路径最稳定。
     * 等局部刷新逻辑修好后，再切回 board_rlcd_flush_area(x1, y1, x2, y2)。
     */
    return board_rlcd_flush();
}
