#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "solar_os_controls.h"

esp_err_t solar_os_stream_get_info(const char *id, solar_os_stream_info_t *info)
{
    if (strcmp(id, "adc1") != 0 && strcmp(id, "gpio1") != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    memset(info, 0, sizeof(*info));
    strcpy(info->id, id);
    info->type = strcmp(id, "adc1") == 0 ?
        SOLAR_OS_STREAM_TYPE_SCALAR : SOLAR_OS_STREAM_TYPE_EVENT;
    return ESP_OK;
}

int main(void)
{
    solar_os_control_config_t config = {
        .name = "cutoff",
        .source = "adc1",
        .input_minimum = 100.0f,
        .input_maximum = 3100.0f,
        .deadband = 10.0f,
        .smoothing_ms = 40U,
    };
    solar_os_control_config_t missing = config;
    strcpy(missing.name, "missing");
    strcpy(missing.source, "adc0");
    assert(solar_os_control_create(&missing) == ESP_ERR_NOT_FOUND);

    solar_os_control_config_t event = config;
    strcpy(event.name, "event");
    strcpy(event.source, "gpio1");
    assert(solar_os_control_create(&event) == ESP_ERR_NOT_SUPPORTED);

    assert(solar_os_control_create(&config) == ESP_OK);
    assert(solar_os_control_create(&config) == ESP_ERR_INVALID_STATE);
    assert(solar_os_control_publish_sample("cutoff", 100.0f, 20U) == ESP_OK);

    uint16_t value = UINT16_MAX;
    assert(solar_os_control_get("cutoff", &value) == ESP_OK);
    assert(value == 0U);
    assert(solar_os_control_publish_sample("cutoff", 3100.0f, 40U) == ESP_OK);
    assert(solar_os_control_get("cutoff", &value) == ESP_OK);
    assert(value > 32000U && value < 34000U);

    solar_os_control_info_t info;
    assert(solar_os_control_find("cutoff", &info) == ESP_OK);
    const uint32_t generation = info.generation;
    assert(solar_os_control_publish_sample("cutoff", 1605.0f, 40U) == ESP_OK);
    assert(solar_os_control_find("cutoff", &info) == ESP_OK);
    assert(info.generation == generation);

    uint32_t parameter_binding = 0U;
    uint32_t midi_binding = 0U;
    assert(solar_os_control_bind_parameter("cutoff", "synth.filter.cutoff",
                                            true, &parameter_binding) == ESP_OK);
    assert(solar_os_control_bind_midi_cc("cutoff", 1U, 74U,
                                         &midi_binding) == ESP_OK);
    assert(parameter_binding != midi_binding);
    assert(solar_os_control_binding_count() == 2U);
    size_t removed = 0U;
    assert(solar_os_control_unbind("cutoff", &removed) == ESP_OK);
    assert(removed == 2U);
    assert(solar_os_control_binding_count() == 0U);
    assert(solar_os_control_unbind("cutoff", &removed) == ESP_ERR_NOT_FOUND);
    assert(removed == 0U);

    solar_os_control_clear();
    assert(solar_os_control_create(&config) == ESP_OK);
    uint32_t reset_binding = 0U;
    assert(solar_os_control_bind_parameter("cutoff", "synth.filter.cutoff",
                                            false, &reset_binding) == ESP_OK);
    assert(reset_binding == 1U);
    assert(solar_os_control_delete("cutoff") == ESP_OK);
    assert(solar_os_control_count() == 0U);
    assert(solar_os_control_binding_count() == 0U);

    puts("controls tests passed");
    return 0;
}
