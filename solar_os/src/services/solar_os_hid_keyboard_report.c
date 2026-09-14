#include "solar_os_hid_keyboard_report.h"

#include <string.h>

static solar_os_hid_keyboard_report_stream_t *report_stream(
    solar_os_hid_keyboard_report_tracker_t *tracker,
    uint8_t map_index,
    uint16_t report_id)
{
    solar_os_hid_keyboard_report_stream_t *free_stream = NULL;
    for (size_t i = 0; i < SOLAR_OS_HID_KEYBOARD_REPORT_STREAMS; i++) {
        solar_os_hid_keyboard_report_stream_t *stream = &tracker->streams[i];
        if (stream->active && stream->map_index == map_index &&
            stream->report_id == report_id) {
            return stream;
        }
        if (!stream->active && free_stream == NULL) {
            free_stream = stream;
        }
    }
    if (free_stream != NULL) {
        free_stream->active = true;
        free_stream->map_index = map_index;
        free_stream->report_id = report_id;
    }
    return free_stream;
}

static bool state_has_key(const solar_os_hid_keyboard_report_state_t *state,
                          uint8_t key)
{
    for (size_t i = 0; i < SOLAR_OS_HID_KEYBOARD_REPORT_KEYS; i++) {
        if (state->keys[i] == key) {
            return true;
        }
    }
    return false;
}

void solar_os_hid_keyboard_report_reset(
    solar_os_hid_keyboard_report_tracker_t *tracker)
{
    if (tracker != NULL) {
        memset(tracker, 0, sizeof(*tracker));
    }
}

bool solar_os_hid_keyboard_report_update(
    solar_os_hid_keyboard_report_tracker_t *tracker,
    uint8_t map_index,
    uint16_t report_id,
    const uint8_t *data,
    size_t length,
    solar_os_hid_keyboard_report_state_t *state)
{
    if (tracker == NULL || data == NULL || state == NULL || length < 7U) {
        return false;
    }

    const size_t key_offset = length == 7U ? 1U : 2U;
    if (length < key_offset + SOLAR_OS_HID_KEYBOARD_REPORT_KEYS) {
        return false;
    }
    solar_os_hid_keyboard_report_stream_t *stream = report_stream(
        tracker,
        map_index,
        report_id);
    if (stream == NULL) {
        return false;
    }
    stream->modifiers = data[0];
    memcpy(stream->keys,
           data + key_offset,
           SOLAR_OS_HID_KEYBOARD_REPORT_KEYS);

    memset(state, 0, sizeof(*state));
    size_t key_count = 0;
    for (size_t i = 0; i < SOLAR_OS_HID_KEYBOARD_REPORT_STREAMS; i++) {
        const solar_os_hid_keyboard_report_stream_t *current =
            &tracker->streams[i];
        if (!current->active) {
            continue;
        }
        state->modifiers |= current->modifiers;
        for (size_t key_index = 0;
             key_index < SOLAR_OS_HID_KEYBOARD_REPORT_KEYS;
             key_index++) {
            const uint8_t key = current->keys[key_index];
            if (key == 0 || state_has_key(state, key)) {
                continue;
            }
            if (key_count < SOLAR_OS_HID_KEYBOARD_REPORT_KEYS) {
                state->keys[key_count++] = key;
            }
        }
    }
    return true;
}
