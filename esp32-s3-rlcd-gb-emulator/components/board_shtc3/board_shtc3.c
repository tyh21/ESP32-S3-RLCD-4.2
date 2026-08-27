#include "board_shtc3.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_shtc3";

/*
 * Waveshare 官方 05_I2C_SHTC3 示例使用：
 *   I2C0 SDA=GPIO13 SCL=GPIO14
 *   SHTC3 7-bit address=0x70
 *
 * 这条 I2C 总线同时也用于 ES8311/ES7210 音频芯片配置，所以这里会先尝试创建总线；
 * 如果总线已经被其他组件创建，就直接复用现有 I2C0。
 */
#define BOARD_SHTC3_I2C_PORT      I2C_NUM_0
#define BOARD_SHTC3_I2C_SDA       GPIO_NUM_13
#define BOARD_SHTC3_I2C_SCL       GPIO_NUM_14
#define BOARD_SHTC3_I2C_SPEED_HZ  400000
#define BOARD_SHTC3_I2C_ADDR      0x70
#define BOARD_SHTC3_I2C_TIMEOUT_MS 1000

#define BOARD_SHTC3_CMD_READ_ID       0xEFC8
#define BOARD_SHTC3_CMD_SOFT_RESET    0x805D
#define BOARD_SHTC3_CMD_SLEEP         0xB098
#define BOARD_SHTC3_CMD_WAKEUP        0x3517
#define BOARD_SHTC3_CMD_MEAS_T_RH     0x7866

/*
 * 官方示例里减了 4 摄氏度，用来抵消板上发热带来的偏高。
 * 这里保留成宏，后续如果实测偏差不同，可以只调这个值。
 */
#define BOARD_SHTC3_TEMPERATURE_OFFSET_C (-4.0f)

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_shtc3_dev = NULL;
static bool s_initialized = false;

static esp_err_t board_shtc3_write_command(uint16_t command)
{
    uint8_t buffer[2] = {
        (uint8_t)(command >> 8),
        (uint8_t)(command & 0xff),
    };

    return i2c_master_transmit(s_shtc3_dev, buffer, sizeof(buffer), BOARD_SHTC3_I2C_TIMEOUT_MS);
}

static uint8_t board_shtc3_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xff;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc & 0x80) != 0) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static esp_err_t board_shtc3_check_crc(const uint8_t *data, uint8_t checksum)
{
    return board_shtc3_crc8(data, 2) == checksum ? ESP_OK : ESP_ERR_INVALID_CRC;
}

static esp_err_t board_shtc3_init_i2c(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_SHTC3_I2C_PORT,
        .sda_io_num = BOARD_SHTC3_I2C_SDA,
        .scl_io_num = BOARD_SHTC3_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = i2c_master_get_bus_handle(BOARD_SHTC3_I2C_PORT, &s_i2c_bus);
    }

    return ret;
}

esp_err_t board_shtc3_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(board_shtc3_init_i2c(), TAG, "init i2c bus failed");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_SHTC3_I2C_ADDR,
        .scl_speed_hz = BOARD_SHTC3_I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_shtc3_dev),
                        TAG,
                        "add shtc3 i2c device failed");

    esp_err_t wake_ret = board_shtc3_write_command(BOARD_SHTC3_CMD_WAKEUP);
    if (wake_ret != ESP_OK) {
        ESP_LOGW(TAG, "wake up command did not ack: %s", esp_err_to_name(wake_ret));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_err_t reset_ret = board_shtc3_write_command(BOARD_SHTC3_CMD_SOFT_RESET);
    if (reset_ret != ESP_OK) {
        ESP_LOGW(TAG, "soft reset command did not ack: %s", esp_err_to_name(reset_ret));
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t command[2] = {
        (uint8_t)(BOARD_SHTC3_CMD_READ_ID >> 8),
        (uint8_t)(BOARD_SHTC3_CMD_READ_ID & 0xff),
    };
    uint8_t id_buffer[3] = {0};
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_shtc3_dev,
                                                    command,
                                                    sizeof(command),
                                                    id_buffer,
                                                    sizeof(id_buffer),
                                                    BOARD_SHTC3_I2C_TIMEOUT_MS),
                        TAG,
                        "read id failed");
    ESP_RETURN_ON_ERROR(board_shtc3_check_crc(id_buffer, id_buffer[2]), TAG, "id crc failed");

    uint16_t id = ((uint16_t)id_buffer[0] << 8) | id_buffer[1];
    ESP_LOGI(TAG, "SHTC3 initialized: id=0x%04x SDA=%d SCL=%d", id, BOARD_SHTC3_I2C_SDA, BOARD_SHTC3_I2C_SCL);

    s_initialized = true;
    return ESP_OK;
}

esp_err_t board_shtc3_sleep(void)
{
    if (!s_initialized || s_shtc3_dev == NULL) {
        return ESP_OK;
    }

    return board_shtc3_write_command(BOARD_SHTC3_CMD_SLEEP);
}

void board_shtc3_release(void)
{
    if (s_shtc3_dev != NULL) {
        (void)i2c_master_bus_rm_device(s_shtc3_dev);
        s_shtc3_dev = NULL;
    }

    /*
     * 不删除 I2C0 总线本身。
     *
     * 这块板子的 SHTC3、ES8311、ES7210 都挂在 GPIO13/14 的同一条 I2C0 上。
     * 进入游戏前只把 SHTC3 这个设备句柄从 ESP-IDF I2C 总线上移除。
     *
     * 注意：这里故意不发送 SLEEP。
     * 实测 SHTC3 进入 sleep 后，后续 WAKEUP 在当前共享 I2C 场景下容易无 ACK；
     * 但只移除 device handle 不会影响 ES8311 音频初始化，回到时钟页后也能继续读温湿度。
     */
    s_initialized = false;
}

void board_shtc3_deinit(void)
{
    board_shtc3_release();

    if (s_i2c_bus != NULL) {
        esp_err_t ret = i2c_del_master_bus(s_i2c_bus);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "delete i2c bus failed: %s", esp_err_to_name(ret));
        } else {
            s_i2c_bus = NULL;
        }
    }
}

esp_err_t board_shtc3_read(board_shtc3_data_t *data)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data is null");
    ESP_RETURN_ON_ERROR(board_shtc3_init(), TAG, "init shtc3 failed");

    esp_err_t ret = board_shtc3_write_command(BOARD_SHTC3_CMD_MEAS_T_RH);
    ESP_RETURN_ON_ERROR(ret, TAG, "start measurement failed");
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t buffer[6] = {0};
    ret = i2c_master_receive(s_shtc3_dev, buffer, sizeof(buffer), BOARD_SHTC3_I2C_TIMEOUT_MS);
    ESP_RETURN_ON_ERROR(ret, TAG, "read measurement failed");

    ESP_RETURN_ON_ERROR(board_shtc3_check_crc(&buffer[0], buffer[2]), TAG, "temperature crc failed");
    ESP_RETURN_ON_ERROR(board_shtc3_check_crc(&buffer[3], buffer[5]), TAG, "humidity crc failed");

    uint16_t raw_temperature = ((uint16_t)buffer[0] << 8) | buffer[1];
    uint16_t raw_humidity = ((uint16_t)buffer[3] << 8) | buffer[4];

    data->temperature_c = -45.0f + 175.0f * (float)raw_temperature / 65536.0f +
                          BOARD_SHTC3_TEMPERATURE_OFFSET_C;
    data->humidity_percent = 100.0f * (float)raw_humidity / 65536.0f;
    return ESP_OK;
}
