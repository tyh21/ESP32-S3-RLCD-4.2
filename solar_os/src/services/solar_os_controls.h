#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_parameters.h"
#include "solar_os_stream.h"

#define SOLAR_OS_CONTROL_NAME_MAX 24U
#define SOLAR_OS_CONTROL_MAX 16U
#define SOLAR_OS_CONTROL_BINDING_MAX 32U
#define SOLAR_OS_CONTROL_NORMALIZED_MAX UINT16_MAX

typedef struct {
    char name[SOLAR_OS_CONTROL_NAME_MAX];
    char source[SOLAR_OS_STREAM_ID_MAX];
    float input_minimum;
    float input_maximum;
    float deadband;
    uint32_t smoothing_ms;
    bool inverted;
} solar_os_control_config_t;

typedef struct {
    solar_os_control_config_t config;
    bool has_value;
    float source_value;
    uint16_t normalized;
    uint32_t generation;
    uint32_t samples;
    uint32_t updates;
    uint32_t read_errors;
    esp_err_t last_error;
} solar_os_control_info_t;

typedef enum {
    SOLAR_OS_CONTROL_TARGET_PARAMETER = 0,
    SOLAR_OS_CONTROL_TARGET_MIDI_CC,
} solar_os_control_target_t;

typedef struct {
    uint32_t id;
    char control[SOLAR_OS_CONTROL_NAME_MAX];
    solar_os_control_target_t target;
    char parameter[SOLAR_OS_PARAMETER_PATH_MAX];
    uint8_t midi_channel;
    uint8_t midi_controller;
    bool pickup;
    bool pickup_seen;
    bool pickup_latched;
    uint16_t pickup_previous;
    uint16_t last_target_value;
    uint32_t last_generation;
    uint32_t applied;
    uint32_t errors;
    esp_err_t last_error;
} solar_os_control_binding_info_t;

esp_err_t solar_os_control_create(const solar_os_control_config_t *config);
esp_err_t solar_os_control_delete(const char *name);
void solar_os_control_clear(void);
size_t solar_os_control_count(void);
bool solar_os_control_get_info(size_t index, solar_os_control_info_t *info);
esp_err_t solar_os_control_find(const char *name, solar_os_control_info_t *info);
esp_err_t solar_os_control_get(const char *name, uint16_t *value);
esp_err_t solar_os_control_set(const char *name, uint16_t value);

/* Worker integration for scalar sources. */
esp_err_t solar_os_control_publish_sample(const char *name,
                                          float source_value,
                                          uint32_t now_ms);
void solar_os_control_note_read_error(const char *name, esp_err_t error);

esp_err_t solar_os_control_bind_parameter(const char *control,
                                          const char *parameter,
                                          bool pickup,
                                          uint32_t *binding_id);
esp_err_t solar_os_control_bind_midi_cc(const char *control,
                                       uint8_t channel,
                                       uint8_t controller,
                                       uint32_t *binding_id);
/* Remove every target binding owned by one named control. */
esp_err_t solar_os_control_unbind(const char *control, size_t *removed);
size_t solar_os_control_binding_count(void);
bool solar_os_control_binding_get(size_t index,
                                  solar_os_control_binding_info_t *info);
void solar_os_control_binding_note(uint32_t binding_id,
                                   uint32_t generation,
                                   bool pickup_seen,
                                   bool pickup_latched,
                                   uint16_t pickup_previous,
                                   uint16_t target_value,
                                   esp_err_t error,
                                   bool applied);

const char *solar_os_control_target_name(solar_os_control_target_t target);
