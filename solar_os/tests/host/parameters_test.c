#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "solar_os_parameters.h"

static float cutoff = 1000.0f;
static float amount = 50.0f;

static esp_err_t value_get(void *user, float *value)
{
    *value = *(float *)user;
    return ESP_OK;
}

static esp_err_t value_set(void *user, float value)
{
    *(float *)user = value;
    return ESP_OK;
}

int main(void)
{
    const solar_os_parameter_definition_t definitions[] = {
        {
            .name = "filter.cutoff",
            .label = "Cutoff",
            .unit = "Hz",
            .minimum = 40.0f,
            .maximum = 18000.0f,
            .step = 1.0f,
            .curve = SOLAR_OS_PARAMETER_CURVE_LOGARITHMIC,
            .get = value_get,
            .set = value_set,
            .user = &cutoff,
        },
        {
            .name = "amount",
            .label = "Amount",
            .unit = "%",
            .minimum = 0.0f,
            .maximum = 100.0f,
            .step = 5.0f,
            .curve = SOLAR_OS_PARAMETER_CURVE_LINEAR,
            .get = value_get,
            .set = value_set,
            .user = &amount,
        },
    };
    solar_os_parameter_registration_t registration =
        SOLAR_OS_PARAMETER_REGISTRATION_INIT;
    assert(solar_os_parameters_register("synth", definitions, 2,
                                         &registration) == ESP_OK);
    assert(solar_os_parameter_count() == 2U);

    solar_os_parameter_info_t info;
    assert(solar_os_parameter_find("synth.filter.cutoff", &info) == ESP_OK);
    assert(info.curve == SOLAR_OS_PARAMETER_CURVE_LOGARITHMIC);

    assert(solar_os_parameter_set("synth.amount", 52.0f) == ESP_OK);
    assert(amount == 50.0f);
    assert(solar_os_parameter_set("synth.amount", 101.0f) ==
           ESP_ERR_INVALID_ARG);

    assert(solar_os_parameter_set_normalized("synth.amount", UINT16_MAX) ==
           ESP_OK);
    assert(amount == 100.0f);
    uint16_t normalized = 0U;
    assert(solar_os_parameter_get_normalized("synth.amount", &normalized) ==
           ESP_OK);
    assert(normalized == UINT16_MAX);

    assert(solar_os_parameter_set_normalized("synth.filter.cutoff", 32768U) ==
           ESP_OK);
    assert(cutoff > 800.0f && cutoff < 900.0f);
    assert(solar_os_parameter_get_normalized("synth.filter.cutoff",
                                              &normalized) == ESP_OK);
    assert(abs((int)normalized - 32768) < 64);

    solar_os_parameter_registration_t duplicate =
        SOLAR_OS_PARAMETER_REGISTRATION_INIT;
    assert(solar_os_parameters_register("synth", definitions, 2, &duplicate) ==
           ESP_ERR_INVALID_STATE);
    assert(solar_os_parameters_unregister(&registration) == ESP_OK);
    assert(solar_os_parameter_count() == 0U);
    assert(solar_os_parameter_get("synth.amount", &amount) ==
           ESP_ERR_NOT_FOUND);

    puts("parameters tests passed");
    return 0;
}
