#include "pwm_port.h"

#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gpio_port.h"

#define PWM_PORT_CHANNEL_COUNT 4
#define PWM_PORT_DUTY_RESOLUTION LEDC_TIMER_10_BIT
#define PWM_PORT_DUTY_RAW_MAX ((1U << 10U) - 1U)

typedef struct {
    bool active;
    gpio_num_t pin;
    ledc_channel_t channel;
    uint32_t freq_hz;
    uint8_t duty_percent;
} pwm_port_slot_t;

static const char *TAG = "pwm_port";

static pwm_port_slot_t pwm_slots[PWM_PORT_CHANNEL_COUNT] = {
    {.channel = LEDC_CHANNEL_0},
    {.channel = LEDC_CHANNEL_1},
    {.channel = LEDC_CHANNEL_2},
    {.channel = LEDC_CHANNEL_3},
};
static SemaphoreHandle_t pwm_mutex;
static uint32_t pwm_timer_freq_hz;

static esp_err_t pwm_port_ensure_init(void)
{
    if (pwm_mutex != NULL) {
        return ESP_OK;
    }

    pwm_mutex = xSemaphoreCreateMutex();
    if (pwm_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static pwm_port_slot_t *pwm_slot_for_pin_locked(gpio_num_t pin)
{
    for (size_t i = 0; i < PWM_PORT_CHANNEL_COUNT; i++) {
        if (pwm_slots[i].active && pwm_slots[i].pin == pin) {
            return &pwm_slots[i];
        }
    }
    return NULL;
}

static pwm_port_slot_t *pwm_alloc_slot_locked(gpio_num_t pin)
{
    pwm_port_slot_t *slot = pwm_slot_for_pin_locked(pin);
    if (slot != NULL) {
        return slot;
    }

    for (size_t i = 0; i < PWM_PORT_CHANNEL_COUNT; i++) {
        if (!pwm_slots[i].active) {
            pwm_slots[i].pin = pin;
            return &pwm_slots[i];
        }
    }
    return NULL;
}

static esp_err_t pwm_config_timer_locked(uint32_t freq_hz)
{
    if (pwm_timer_freq_hz == freq_hz) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = PWM_PORT_DUTY_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "LEDC timer config failed");

    pwm_timer_freq_hz = freq_hz;
    for (size_t i = 0; i < PWM_PORT_CHANNEL_COUNT; i++) {
        if (pwm_slots[i].active) {
            pwm_slots[i].freq_hz = freq_hz;
        }
    }
    return ESP_OK;
}

esp_err_t pwm_port_init(void)
{
    return pwm_port_ensure_init();
}

esp_err_t pwm_port_set(gpio_num_t pin, uint32_t freq_hz, uint8_t duty_percent)
{
    if (!gpio_port_is_valid_output_pin(pin) ||
        freq_hz < PWM_PORT_FREQ_MIN_HZ ||
        freq_hz > PWM_PORT_FREQ_MAX_HZ ||
        duty_percent > PWM_PORT_DUTY_MAX_PERCENT) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(pwm_port_ensure_init(), TAG, "PWM port init failed");
    xSemaphoreTake(pwm_mutex, portMAX_DELAY);

    pwm_port_slot_t *slot = pwm_alloc_slot_locked(pin);
    if (slot == NULL) {
        xSemaphoreGive(pwm_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = pwm_config_timer_locked(freq_hz);
    if (ret == ESP_OK) {
        const uint32_t duty_raw =
            ((uint32_t)duty_percent * PWM_PORT_DUTY_RAW_MAX + 50U) / 100U;
        ledc_channel_config_t channel_config = {
            .gpio_num = pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = slot->channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = duty_raw,
            .hpoint = 0,
            .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        };
        ret = ledc_channel_config(&channel_config);
        if (ret == ESP_OK) {
            slot->active = true;
            slot->pin = pin;
            slot->freq_hz = freq_hz;
            slot->duty_percent = duty_percent;
        }
    }

    xSemaphoreGive(pwm_mutex);
    return ret;
}

esp_err_t pwm_port_stop(gpio_num_t pin)
{
    ESP_RETURN_ON_ERROR(pwm_port_ensure_init(), TAG, "PWM port init failed");
    xSemaphoreTake(pwm_mutex, portMAX_DELAY);

    pwm_port_slot_t *slot = pwm_slot_for_pin_locked(pin);
    if (slot == NULL) {
        xSemaphoreGive(pwm_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    const esp_err_t ret = ledc_stop(LEDC_LOW_SPEED_MODE, slot->channel, 0);
    if (ret == ESP_OK) {
        slot->active = false;
        slot->freq_hz = 0;
        slot->duty_percent = 0;
    }

    xSemaphoreGive(pwm_mutex);
    return ret;
}

bool pwm_port_get(gpio_num_t pin, pwm_port_status_t *status)
{
    if (status == NULL || pwm_port_ensure_init() != ESP_OK) {
        return false;
    }

    xSemaphoreTake(pwm_mutex, portMAX_DELAY);

    pwm_port_slot_t *slot = pwm_slot_for_pin_locked(pin);
    if (slot == NULL) {
        *status = (pwm_port_status_t) {
            .pin = pin,
            .active = false,
        };
        xSemaphoreGive(pwm_mutex);
        return true;
    }

    *status = (pwm_port_status_t) {
        .pin = slot->pin,
        .active = slot->active,
        .channel = slot->channel,
        .freq_hz = slot->freq_hz,
        .duty_percent = slot->duty_percent,
    };
    xSemaphoreGive(pwm_mutex);
    return true;
}
