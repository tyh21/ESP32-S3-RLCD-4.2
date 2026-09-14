#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*solar_os_vt100_emit_fn)(char ch, void *user);

typedef enum {
    SOLAR_OS_VT100_STATE_NORMAL,
    SOLAR_OS_VT100_STATE_ESC,
    SOLAR_OS_VT100_STATE_CSI,
    SOLAR_OS_VT100_STATE_SS3,
} solar_os_vt100_state_t;

typedef struct {
    solar_os_vt100_state_t state;
    char sequence[24];
    size_t sequence_len;
} solar_os_vt100_input_t;

void solar_os_vt100_input_init(solar_os_vt100_input_t *input);
void solar_os_vt100_input_reset(solar_os_vt100_input_t *input);
bool solar_os_vt100_input_pending(const solar_os_vt100_input_t *input);
bool solar_os_vt100_input_feed_byte(solar_os_vt100_input_t *input,
                                    uint8_t byte,
                                    solar_os_vt100_emit_fn emit,
                                    void *user);
size_t solar_os_vt100_input_feed(solar_os_vt100_input_t *input,
                                 const uint8_t *data,
                                 size_t len,
                                 solar_os_vt100_emit_fn emit,
                                 void *user);
bool solar_os_vt100_input_flush(solar_os_vt100_input_t *input,
                                solar_os_vt100_emit_fn emit,
                                void *user);
