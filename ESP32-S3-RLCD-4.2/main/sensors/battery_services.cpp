// 负责电池电压采样、电量估算和充电状态判断。
#include "sensor_services.h"

#include "app_time_constants.h"
#include "battery_charging_state.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "ui_views.h"

#define BATTERY_ADC_CALIBRATION_RELEASE_FAILED_LOG_FORMAT "battery adc calibration release failed: %s"
#define BATTERY_ADC_UNIT_RELEASE_FAILED_LOG_FORMAT "battery adc unit release failed: %s"
#define BATTERY_ADC_READY_WITHOUT_HANDLE_LOG_FORMAT "battery adc marked ready without handle, resetting"
#define BATTERY_ADC_CALIBRATION_READY_WITHOUT_HANDLE_LOG "battery adc calibration marked ready without handle, disabling calibration"
#define BATTERY_ADC_INIT_FAILED_LOG_FORMAT "battery adc init failed: %s"
#define BATTERY_ADC_CHANNEL_CONFIG_FAILED_LOG_FORMAT "battery adc channel config failed: %s"
#define BATTERY_ADC_CALIBRATION_UNAVAILABLE_LOG_FORMAT "battery adc calibration unavailable: %s"
#define BATTERY_PERCENT_OUTPUT_NULL_LOG_FORMAT "battery percent output is null"
#define BATTERY_ADC_READ_FAILED_LOG_FORMAT "battery adc read failed: %s"
#define BATTERY_ADC_CALIBRATION_READ_FAILED_LOG_FORMAT "battery adc calibration read failed: %s"
#define BATTERY_ADC_SAMPLE_LOG_FORMAT "battery adc raw=%d adc_mv=%d battery=%.3fV soc=%d%%"
#define BATTERY_CHARGING_STARTED_LOG_FORMAT "battery charging detected voltage=%.3fV soc=%d%%"
#define BATTERY_CHARGING_ANIMATION_COMPLETED_LOG_FORMAT "battery charging animation completed voltage=%.3fV soc=%d%%"
#define BATTERY_CHARGING_STOPPED_LOG_FORMAT "battery charging cleared voltage=%.3fV soc=%d%%"

static adc_oneshot_unit_handle_t s_battery_adc = nullptr;
static adc_cali_handle_t s_battery_adc_cali = nullptr;
static bool s_battery_adc_ready = false;
static bool s_battery_adc_cali_ready = false;
static constexpr float kBatteryVoltageDivider = 3.0f;
static constexpr float kBatteryMillivoltsToVolts = 0.001f;
static constexpr int kBatteryChargingStopAdcSteps = 2;
static constexpr float kBatteryEmptyVoltage = 3.00f;
static constexpr float kBatteryFullVoltage = 4.12f;
static constexpr float kBatteryVoltageRange = kBatteryFullVoltage - kBatteryEmptyVoltage;
static constexpr float kBatteryPercentScale = 100.0f;
static constexpr float kBatteryPercentRoundOffset = 0.5f;
static constexpr float kBatteryValidPreviousVoltageMin = kBatteryEmptyVoltage;
static constexpr adc_unit_t kBatteryAdcUnit = ADC_UNIT_1;
static constexpr adc_channel_t kBatteryAdcChannel = ADC_CHANNEL_3;
static constexpr adc_bitwidth_t kBatteryAdcBitwidth = ADC_BITWIDTH_12;
static constexpr adc_atten_t kBatteryAdcAtten = ADC_ATTEN_DB_12;
static constexpr int kBatteryAdcReferenceMv = 3300;
static constexpr int kBatteryAdcRawMax = 4095;
static constexpr int kBatteryPercentMin = 0;
static constexpr int kBatteryPercentMax = 100;
static constexpr int kBatteryPercentUnknown = -1;
static constexpr int kBatteryMinValidYear = 2023;
static constexpr int kBatteryMinValidTmYear = kBatteryMinValidYear - kTmYearOffset;
static constexpr uint32_t kBatteryChargingAnimationIdleTicks =
    pdMS_TO_TICKS(kBatteryChargingAnimationIdleMs);
static constexpr BatteryChargingPolicy kBatteryChargingPolicy = {
    kBatteryValidPreviousVoltageMin,
    kBatteryChargingRiseVoltage,
    kBatteryChargingStopVoltage,
    kBatteryChargingRiseSamples,
    kBatteryChargingStopSamples,
    kBatteryChargingAnimationStopPercent,
    kBatteryChargingAnimationIdleTicks,
};

struct BatteryReading {
    int percent = kBatteryPercentUnknown;
    float voltage = 0.0f;
};

static_assert(kBatteryVoltageDivider > 0.0f, "battery voltage divider must be positive");
static_assert(kBatteryMillivoltsToVolts > 0.0f, "millivolts-to-volts scale must be positive");
static_assert(kBatteryFullVoltage > kBatteryEmptyVoltage, "battery voltage range must be positive");
static_assert(kBatteryPercentScale > 0.0f, "battery percent scale must be positive");
static_assert(kBatteryPercentRoundOffset >= 0.0f && kBatteryPercentRoundOffset < 1.0f,
              "battery percent rounding offset must stay within one percent step");
static_assert(kBatteryValidPreviousVoltageMin >= kBatteryEmptyVoltage &&
                  kBatteryValidPreviousVoltageMin < kBatteryFullVoltage,
              "battery previous voltage must stay within plausible battery range");
static_assert(kBatteryAdcReferenceMv > 0, "battery ADC reference voltage must be positive");
static_assert(kBatteryAdcRawMax == (1 << 12) - 1, "12-bit battery ADC raw max must stay 4095");
static_assert(kBatteryPercentMin < kBatteryPercentMax, "battery percent range must be ordered");
static_assert(kBatteryPercentUnknown < kBatteryPercentMin, "unknown battery percent must be below valid range");
static_assert(kBatteryMinValidYear > kTmYearOffset, "battery valid year must exceed tm year offset");
static_assert(kBatteryChargingRiseSamples > 0, "charging detection must require at least one rising sample");
static_assert(kBatteryChargingStopSamples > 0,
              "charging stop detection must require at least one confirming sample");
static_assert(kBatteryChargingAnimationStopPercent > kBatteryPercentMin &&
                  kBatteryChargingAnimationStopPercent <= kBatteryPercentMax,
              "charging animation stop percent must stay within the battery range");
static_assert(kBatteryChargingAnimationIdleTicks > 0,
              "charging animation idle tick timeout must be positive");
static_assert(sizeof(TickType_t) == sizeof(uint32_t),
              "battery charging tracker expects 32-bit FreeRTOS ticks");
static_assert(kBatteryChargingRiseVoltage > kBatteryChargingStopVoltage,
              "charging rise threshold must stay above stop threshold");
static_assert(kBatteryChargingStopVoltage >=
                  kBatteryVoltageDivider * kBatteryMillivoltsToVolts * kBatteryChargingStopAdcSteps,
              "charging stop threshold must cover at least two scaled ADC millivolt steps");

static int clamp_battery_percent(int percent)
{
    if (percent < kBatteryPercentMin) {
        return kBatteryPercentMin;
    }
    if (percent > kBatteryPercentMax) {
        return kBatteryPercentMax;
    }
    return percent;
}

static bool battery_time_valid(time_t value)
{
    if (value <= 0) {
        return false;
    }
    struct tm local = {};
    return localtime_r(&value, &local) && local.tm_year >= kBatteryMinValidTmYear;
}

static time_t last_charge_time_after_unplug(time_t previous)
{
    time_t now = 0;
    time(&now);
    if (battery_time_valid(now)) {
        return now;
    }
    return previous;
}

void release_battery_gauge()
{
    if (s_battery_adc_cali_ready && s_battery_adc_cali) {
        esp_err_t err = adc_cali_delete_scheme_curve_fitting(s_battery_adc_cali);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, BATTERY_ADC_CALIBRATION_RELEASE_FAILED_LOG_FORMAT, esp_err_to_name(err));
        }
    }
    s_battery_adc_cali = nullptr;
    s_battery_adc_cali_ready = false;

    if (s_battery_adc) {
        esp_err_t err = adc_oneshot_del_unit(s_battery_adc);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, BATTERY_ADC_UNIT_RELEASE_FAILED_LOG_FORMAT, esp_err_to_name(err));
        }
    }
    s_battery_adc = nullptr;
    s_battery_adc_ready = false;
}

bool init_battery_gauge()
{
    if (s_battery_adc_ready) {
        if (!s_battery_adc) {
            ESP_LOGW(TAG, BATTERY_ADC_READY_WITHOUT_HANDLE_LOG_FORMAT);
            release_battery_gauge();
            return false;
        }
        if (s_battery_adc_cali_ready && !s_battery_adc_cali) {
            ESP_LOGW(TAG, "%s", BATTERY_ADC_CALIBRATION_READY_WITHOUT_HANDLE_LOG);
            s_battery_adc_cali_ready = false;
        }
        return true;
    }

    adc_oneshot_unit_init_cfg_t init_config = {};
    init_config.unit_id = kBatteryAdcUnit;
    esp_err_t err = adc_oneshot_new_unit(&init_config, &s_battery_adc);
    if (err != ESP_OK) {
        s_battery_adc = nullptr;
        ESP_LOGW(TAG, BATTERY_ADC_INIT_FAILED_LOG_FORMAT, esp_err_to_name(err));
        return false;
    }

    adc_oneshot_chan_cfg_t chan_config = {};
    chan_config.bitwidth = kBatteryAdcBitwidth;
    chan_config.atten = kBatteryAdcAtten;
    err = adc_oneshot_config_channel(s_battery_adc, kBatteryAdcChannel, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, BATTERY_ADC_CHANNEL_CONFIG_FAILED_LOG_FORMAT, esp_err_to_name(err));
        release_battery_gauge();
        return false;
    }

    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = kBatteryAdcUnit;
    cali_config.chan = kBatteryAdcChannel;
    cali_config.atten = kBatteryAdcAtten;
    cali_config.bitwidth = kBatteryAdcBitwidth;
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &s_battery_adc_cali);
    if (err == ESP_OK && s_battery_adc_cali) {
        s_battery_adc_cali_ready = true;
    } else {
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "%s", BATTERY_ADC_CALIBRATION_READY_WITHOUT_HANDLE_LOG);
        } else {
            ESP_LOGW(TAG, BATTERY_ADC_CALIBRATION_UNAVAILABLE_LOG_FORMAT, esp_err_to_name(err));
        }
        s_battery_adc_cali = nullptr;
        s_battery_adc_cali_ready = false;
    }

    s_battery_adc_ready = true;
    return true;
}

int battery_percent_from_voltage(float voltage)
{
    int percent = (int)(((voltage - kBatteryEmptyVoltage) * kBatteryPercentScale /
                         kBatteryVoltageRange) + kBatteryPercentRoundOffset);
    return clamp_battery_percent(percent);
}

int battery_adc_raw_to_mv(int raw)
{
    return (raw * kBatteryAdcReferenceMv) / kBatteryAdcRawMax;
}

float battery_voltage_from_adc_mv(int adc_mv)
{
    return adc_mv * kBatteryMillivoltsToVolts * kBatteryVoltageDivider;
}

static bool read_battery_reading(BatteryReading *reading)
{
    if (!reading) {
        ESP_LOGW(TAG, BATTERY_PERCENT_OUTPUT_NULL_LOG_FORMAT);
        return false;
    }
    if (!init_battery_gauge()) {
        return false;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_battery_adc, kBatteryAdcChannel, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, BATTERY_ADC_READ_FAILED_LOG_FORMAT, esp_err_to_name(err));
        release_battery_gauge();
        return false;
    }

    int adc_mv = battery_adc_raw_to_mv(raw);
    if (s_battery_adc_cali_ready) {
        err = adc_cali_raw_to_voltage(s_battery_adc_cali, raw, &adc_mv);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, BATTERY_ADC_CALIBRATION_READ_FAILED_LOG_FORMAT, esp_err_to_name(err));
        }
    }

    float voltage = battery_voltage_from_adc_mv(adc_mv);
    int soc = battery_percent_from_voltage(voltage);
    ESP_LOGI(TAG, BATTERY_ADC_SAMPLE_LOG_FORMAT, raw, adc_mv, voltage, soc);
    reading->percent = soc;
    reading->voltage = voltage;
    return true;
}

bool read_battery_percent(int *percent)
{
    if (!percent) {
        ESP_LOGW(TAG, BATTERY_PERCENT_OUTPUT_NULL_LOG_FORMAT);
        return false;
    }
    BatteryReading reading;
    if (!read_battery_reading(&reading)) {
        return false;
    }
    *percent = reading.percent;
    // Preserve the existing public helper behavior for any external caller.
    battery_runtime_voltage_store(reading.voltage);
    return true;
}

static BatteryChargingState derive_battery_charging_state(
    const BatteryReading &reading,
    float previous_voltage,
    const BatteryChargingState &previous_state,
    BatteryChargingTracker &tracker)
{
    BatteryChargingInput input = {
        previous_voltage,
        reading.voltage,
        reading.percent,
        static_cast<uint32_t>(xTaskGetTickCount()),
    };
    BatteryChargingState state = previous_state;
    (void)update_battery_charging_state(input,
                                        kBatteryChargingPolicy,
                                        &tracker,
                                        &state);
    return state;
}

static void apply_battery_reading(const BatteryReading &reading,
                                  const BatteryChargingState &state,
                                  BatteryRuntimeSnapshot *snapshot)
{
    if (!snapshot) {
        return;
    }
    snapshot->percent = reading.percent;
    snapshot->voltage = reading.voltage;
    snapshot->charging = state.charging;
    snapshot->animation_complete = state.animation_complete;
}

static void reset_battery_state_after_sample_failure(BatteryChargingTracker &tracker,
                                                     BatteryRuntimeSnapshot *snapshot)
{
    if (snapshot) {
        snapshot->percent = kBatteryPercentUnknown;
        snapshot->voltage = 0.0f;
        snapshot->charging = false;
        snapshot->animation_complete = false;
    }
    reset_battery_charging_tracker(&tracker);
}

static bool battery_visible_state_changed(const BatteryRuntimeSnapshot &previous,
                                          const BatteryRuntimeSnapshot &next)
{
    return previous.percent != next.percent ||
           previous.charging != next.charging ||
           previous.animation_complete != next.animation_complete ||
           previous.low_battery_mode != next.low_battery_mode;
}

void sample_battery()
{
    static BatteryChargingTracker charging_tracker;
    BatteryRuntimeSnapshot previous;
    battery_runtime_snapshot_load(&previous);
    BatteryRuntimeSnapshot next = previous;
    BatteryReading reading;
    if (read_battery_reading(&reading)) {
        BatteryChargingState previous_state = {
            previous.charging,
            previous.animation_complete,
        };
        BatteryChargingState next_state = derive_battery_charging_state(
            reading,
            previous.voltage,
            previous_state,
            charging_tracker);
        apply_battery_reading(reading, next_state, &next);
        if (!previous.charging && next.charging) {
            ESP_LOGI(TAG, BATTERY_CHARGING_STARTED_LOG_FORMAT, next.voltage, next.percent);
        }
        if (!previous.animation_complete && next.animation_complete) {
            ESP_LOGI(TAG,
                     BATTERY_CHARGING_ANIMATION_COMPLETED_LOG_FORMAT,
                     next.voltage,
                     next.percent);
        }
        if (previous.charging && !next.charging) {
            ESP_LOGI(TAG, BATTERY_CHARGING_STOPPED_LOG_FORMAT, next.voltage, next.percent);
            next.last_charge_time = last_charge_time_after_unplug(previous.last_charge_time);
        }
        release_battery_gauge();
    } else {
        reset_battery_state_after_sample_failure(charging_tracker, &next);
    }
    next.low_battery_mode = battery_low_mode_for_percent(previous.low_battery_mode,
                                                         next.percent,
                                                         kLowBatteryEnterPercent,
                                                         kLowBatteryExitPercent);
    bool state_changed = battery_visible_state_changed(previous, next);
    if (state_changed) {
        ++next.version;
    }
    battery_runtime_snapshot_store(next);
    if (state_changed) {
        notify_ui_task();
    }
}
