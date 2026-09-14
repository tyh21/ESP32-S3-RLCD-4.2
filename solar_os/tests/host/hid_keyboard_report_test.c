#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "solar_os_hid_keyboard_report.h"

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

int main(void)
{
    solar_os_hid_keyboard_report_tracker_t tracker = {0};
    solar_os_hid_keyboard_report_state_t state;

    const uint8_t boot_a[8] = {0, 0, 0x04};
    assert(solar_os_hid_keyboard_report_update(&tracker,
                                               0,
                                               1,
                                               boot_a,
                                               sizeof(boot_a),
                                               &state));
    assert(state.modifiers == 0);
    assert(state_has_key(&state, 0x04));

    /* An idle report from another collection must not release map 0's key. */
    const uint8_t boot_idle[8] = {0};
    assert(solar_os_hid_keyboard_report_update(&tracker,
                                               1,
                                               2,
                                               boot_idle,
                                               sizeof(boot_idle),
                                               &state));
    assert(state_has_key(&state, 0x04));

    /* Mirrored reports are de-duplicated and remain held until both release. */
    assert(solar_os_hid_keyboard_report_update(&tracker,
                                               1,
                                               2,
                                               boot_a,
                                               sizeof(boot_a),
                                               &state));
    assert(state.keys[0] == 0x04);
    assert(state.keys[1] == 0);
    assert(solar_os_hid_keyboard_report_update(&tracker,
                                               0,
                                               1,
                                               boot_idle,
                                               sizeof(boot_idle),
                                               &state));
    assert(state_has_key(&state, 0x04));
    assert(solar_os_hid_keyboard_report_update(&tracker,
                                               1,
                                               2,
                                               boot_idle,
                                               sizeof(boot_idle),
                                               &state));
    assert(!state_has_key(&state, 0x04));

    /* Report protocol can omit the boot report's reserved byte. */
    const uint8_t report_shift_b[7] = {0x02, 0x05};
    assert(solar_os_hid_keyboard_report_update(&tracker,
                                               2,
                                               3,
                                               report_shift_b,
                                               sizeof(report_shift_b),
                                               &state));
    assert(state.modifiers == 0x02);
    assert(state_has_key(&state, 0x05));

    const solar_os_hid_keyboard_report_state_t previous = state;
    const uint8_t invalid[6] = {0};
    assert(!solar_os_hid_keyboard_report_update(&tracker,
                                                2,
                                                3,
                                                invalid,
                                                sizeof(invalid),
                                                &state));
    assert(memcmp(&state, &previous, sizeof(state)) == 0);

    solar_os_hid_keyboard_report_reset(&tracker);
    puts("HID keyboard report tests: ok");
    return 0;
}
