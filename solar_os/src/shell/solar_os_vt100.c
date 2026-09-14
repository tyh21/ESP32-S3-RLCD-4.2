#include "solar_os_vt100.h"

#include <ctype.h>
#include <string.h>

#include "solar_os_keys.h"

#define VT100_MAX_PARAMS 4

typedef struct {
    int value[VT100_MAX_PARAMS];
    size_t count;
} vt100_params_t;

static bool vt100_emit(solar_os_vt100_emit_fn emit, void *user, char ch)
{
    return emit == NULL || emit(ch, user);
}

static bool vt100_emit_alt(solar_os_vt100_emit_fn emit, void *user, char ch)
{
    return vt100_emit(emit, user, (char)SOLAR_OS_KEY_ALT_PREFIX) &&
        vt100_emit(emit, user, ch);
}

static bool vt100_emit_modified(solar_os_vt100_emit_fn emit,
                                void *user,
                                char ch,
                                bool alt)
{
    return alt ? vt100_emit_alt(emit, user, ch) : vt100_emit(emit, user, ch);
}

void solar_os_vt100_input_init(solar_os_vt100_input_t *input)
{
    if (input == NULL) {
        return;
    }

    memset(input, 0, sizeof(*input));
    input->state = SOLAR_OS_VT100_STATE_NORMAL;
}

void solar_os_vt100_input_reset(solar_os_vt100_input_t *input)
{
    solar_os_vt100_input_init(input);
}

bool solar_os_vt100_input_pending(const solar_os_vt100_input_t *input)
{
    return input != NULL && input->state != SOLAR_OS_VT100_STATE_NORMAL;
}

static void vt100_sequence_reset(solar_os_vt100_input_t *input)
{
    if (input == NULL) {
        return;
    }

    input->state = SOLAR_OS_VT100_STATE_NORMAL;
    input->sequence_len = 0;
    input->sequence[0] = '\0';
}

static bool vt100_parse_params(const char *text, size_t len, vt100_params_t *params)
{
    int current = -1;

    if (params == NULL) {
        return false;
    }

    memset(params, 0, sizeof(*params));
    for (size_t i = 0; i < len; i++) {
        const char ch = text[i];
        if (ch == '?' || ch == '>' || ch == '<' || ch == '=') {
            continue;
        }
        if (isdigit((unsigned char)ch)) {
            if (current < 0) {
                current = 0;
            }
            current = (current * 10) + (ch - '0');
            continue;
        }
        if (ch == ';') {
            if (params->count < VT100_MAX_PARAMS) {
                params->value[params->count++] = current < 0 ? 0 : current;
            }
            current = -1;
            continue;
        }
    }

    if (current >= 0 || (len > 0 && text[len - 1] == ';')) {
        if (params->count < VT100_MAX_PARAMS) {
            params->value[params->count++] = current < 0 ? 0 : current;
        }
    }
    return true;
}

static int vt100_param_or(const vt100_params_t *params, size_t index, int fallback)
{
    if (params == NULL || index >= params->count || params->value[index] == 0) {
        return fallback;
    }
    return params->value[index];
}

static void vt100_decode_modifier(int modifier, bool *shift, bool *alt, bool *ctrl)
{
    const int flags = modifier > 1 ? modifier - 1 : 0;

    if (shift != NULL) {
        *shift = (flags & 0x01) != 0;
    }
    if (alt != NULL) {
        *alt = (flags & 0x02) != 0;
    }
    if (ctrl != NULL) {
        *ctrl = (flags & 0x04) != 0;
    }
}

static char vt100_arrow_key(char final, bool shift, bool ctrl)
{
    switch (final) {
    case 'A':
        if (ctrl && shift) {
            return (char)SOLAR_OS_KEY_CTRL_SHIFT_UP;
        }
        if (ctrl) {
            return (char)SOLAR_OS_KEY_CTRL_UP;
        }
        return shift ? (char)SOLAR_OS_KEY_SHIFT_UP : (char)SOLAR_OS_KEY_UP;
    case 'B':
        if (ctrl && shift) {
            return (char)SOLAR_OS_KEY_CTRL_SHIFT_DOWN;
        }
        if (ctrl) {
            return (char)SOLAR_OS_KEY_CTRL_DOWN;
        }
        return shift ? (char)SOLAR_OS_KEY_SHIFT_DOWN : (char)SOLAR_OS_KEY_DOWN;
    case 'C':
        if (ctrl && shift) {
            return (char)SOLAR_OS_KEY_CTRL_SHIFT_RIGHT;
        }
        if (ctrl) {
            return (char)SOLAR_OS_KEY_CTRL_RIGHT;
        }
        return shift ? (char)SOLAR_OS_KEY_SHIFT_RIGHT : (char)SOLAR_OS_KEY_RIGHT;
    case 'D':
        if (ctrl && shift) {
            return (char)SOLAR_OS_KEY_CTRL_SHIFT_LEFT;
        }
        if (ctrl) {
            return (char)SOLAR_OS_KEY_CTRL_LEFT;
        }
        return shift ? (char)SOLAR_OS_KEY_SHIFT_LEFT : (char)SOLAR_OS_KEY_LEFT;
    default:
        return '\0';
    }
}

static char vt100_home_end_key(char final, bool shift, bool ctrl)
{
    const bool home = final == 'H';

    if (ctrl && shift) {
        return home ? (char)SOLAR_OS_KEY_CTRL_SHIFT_HOME : (char)SOLAR_OS_KEY_CTRL_SHIFT_END;
    }
    if (ctrl) {
        return home ? (char)SOLAR_OS_KEY_CTRL_HOME : (char)SOLAR_OS_KEY_CTRL_END;
    }
    if (shift) {
        return home ? (char)SOLAR_OS_KEY_SHIFT_HOME : (char)SOLAR_OS_KEY_SHIFT_END;
    }
    return home ? (char)SOLAR_OS_KEY_HOME : (char)SOLAR_OS_KEY_END;
}

static char vt100_page_key(bool page_up, bool shift)
{
    if (page_up) {
        return shift ? (char)SOLAR_OS_KEY_SHIFT_PAGE_UP : (char)SOLAR_OS_KEY_PAGE_UP;
    }
    return shift ? (char)SOLAR_OS_KEY_SHIFT_PAGE_DOWN : (char)SOLAR_OS_KEY_PAGE_DOWN;
}

static char vt100_function_key(int code)
{
    switch (code) {
    case 11:
        return (char)SOLAR_OS_KEY_F1;
    case 12:
        return (char)SOLAR_OS_KEY_F2;
    case 13:
        return (char)SOLAR_OS_KEY_F3;
    case 14:
        return (char)SOLAR_OS_KEY_F4;
    case 15:
        return (char)SOLAR_OS_KEY_F5;
    case 17:
        return (char)SOLAR_OS_KEY_F6;
    case 18:
        return (char)SOLAR_OS_KEY_F7;
    case 19:
        return (char)SOLAR_OS_KEY_F8;
    case 20:
        return (char)SOLAR_OS_KEY_F9;
    case 21:
        return (char)SOLAR_OS_KEY_F10;
    case 23:
        return (char)SOLAR_OS_KEY_F11;
    case 24:
        return (char)SOLAR_OS_KEY_F12;
    default:
        return '\0';
    }
}

static bool vt100_emit_csi_tilde(const vt100_params_t *params,
                                 solar_os_vt100_emit_fn emit,
                                 void *user)
{
    const int code = vt100_param_or(params, 0, 0);
    const int modifier = vt100_param_or(params, 1, 1);
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
    char key = '\0';

    vt100_decode_modifier(modifier, &shift, &alt, &ctrl);

    switch (code) {
    case 1:
    case 7:
        key = ctrl ?
            (shift ? (char)SOLAR_OS_KEY_CTRL_SHIFT_HOME : (char)SOLAR_OS_KEY_CTRL_HOME) :
            (shift ? (char)SOLAR_OS_KEY_SHIFT_HOME : (char)SOLAR_OS_KEY_HOME);
        break;
    case 3:
        key = (char)SOLAR_OS_KEY_DELETE;
        break;
    case 4:
    case 8:
        key = ctrl ?
            (shift ? (char)SOLAR_OS_KEY_CTRL_SHIFT_END : (char)SOLAR_OS_KEY_CTRL_END) :
            (shift ? (char)SOLAR_OS_KEY_SHIFT_END : (char)SOLAR_OS_KEY_END);
        break;
    case 5:
        key = vt100_page_key(true, shift);
        break;
    case 6:
        key = vt100_page_key(false, shift);
        break;
    default:
        key = vt100_function_key(code);
        break;
    }

    return key == '\0' || vt100_emit_modified(emit, user, key, alt);
}

static bool vt100_emit_csi(solar_os_vt100_input_t *input,
                           char final,
                           solar_os_vt100_emit_fn emit,
                           void *user)
{
    vt100_params_t params;
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
    char key = '\0';

    vt100_parse_params(input->sequence, input->sequence_len, &params);
    vt100_decode_modifier(vt100_param_or(&params, 1, 1), &shift, &alt, &ctrl);

    if (final >= 'A' && final <= 'D') {
        key = vt100_arrow_key(final, shift, ctrl);
    } else if (final == 'H' || final == 'F') {
        key = vt100_home_end_key(final, shift, ctrl);
    } else if (final == '~') {
        return vt100_emit_csi_tilde(&params, emit, user);
    } else if (final == 'Z') {
        key = '\t';
    }

    return key == '\0' || vt100_emit_modified(emit, user, key, alt);
}

static bool vt100_emit_ss3(char final, solar_os_vt100_emit_fn emit, void *user)
{
    char key = '\0';

    switch (final) {
    case 'A':
        key = (char)SOLAR_OS_KEY_UP;
        break;
    case 'B':
        key = (char)SOLAR_OS_KEY_DOWN;
        break;
    case 'C':
        key = (char)SOLAR_OS_KEY_RIGHT;
        break;
    case 'D':
        key = (char)SOLAR_OS_KEY_LEFT;
        break;
    case 'H':
        key = (char)SOLAR_OS_KEY_HOME;
        break;
    case 'F':
        key = (char)SOLAR_OS_KEY_END;
        break;
    case 'P':
        key = (char)SOLAR_OS_KEY_F1;
        break;
    case 'Q':
        key = (char)SOLAR_OS_KEY_F2;
        break;
    case 'R':
        key = (char)SOLAR_OS_KEY_F3;
        break;
    case 'S':
        key = (char)SOLAR_OS_KEY_F4;
        break;
    default:
        break;
    }

    return key == '\0' || vt100_emit(emit, user, key);
}

bool solar_os_vt100_input_feed_byte(solar_os_vt100_input_t *input,
                                    uint8_t byte,
                                    solar_os_vt100_emit_fn emit,
                                    void *user)
{
    if (input == NULL) {
        return false;
    }

    switch (input->state) {
    case SOLAR_OS_VT100_STATE_NORMAL:
        if (byte == 0x1b) {
            input->state = SOLAR_OS_VT100_STATE_ESC;
            input->sequence_len = 0;
            return true;
        }
        if (byte == 0x1d) {
            return vt100_emit(emit, user, (char)SOLAR_OS_KEY_APP_EXIT);
        }
        if (byte == 0x7f) {
            return vt100_emit(emit, user, '\b');
        }
        return vt100_emit(emit, user, (char)byte);

    case SOLAR_OS_VT100_STATE_ESC:
        if (byte == '[') {
            input->state = SOLAR_OS_VT100_STATE_CSI;
            input->sequence_len = 0;
            input->sequence[0] = '\0';
            return true;
        }
        if (byte == 'O') {
            input->state = SOLAR_OS_VT100_STATE_SS3;
            return true;
        }
        if (byte == 0x1b) {
            if (!vt100_emit(emit, user, (char)SOLAR_OS_KEY_ESCAPE)) {
                return false;
            }
            input->state = SOLAR_OS_VT100_STATE_ESC;
            return true;
        }
        vt100_sequence_reset(input);
        return vt100_emit_alt(emit, user, byte == 0x7f ? '\b' : (char)byte);

    case SOLAR_OS_VT100_STATE_CSI:
        if (byte >= 0x40 && byte <= 0x7e) {
            const bool ok = vt100_emit_csi(input, (char)byte, emit, user);
            vt100_sequence_reset(input);
            return ok;
        }
        if (input->sequence_len + 1 >= sizeof(input->sequence)) {
            vt100_sequence_reset(input);
            return true;
        }
        input->sequence[input->sequence_len++] = (char)byte;
        input->sequence[input->sequence_len] = '\0';
        return true;

    case SOLAR_OS_VT100_STATE_SS3: {
        const bool ok = vt100_emit_ss3((char)byte, emit, user);
        vt100_sequence_reset(input);
        return ok;
    }
    default:
        vt100_sequence_reset(input);
        return true;
    }
}

size_t solar_os_vt100_input_feed(solar_os_vt100_input_t *input,
                                 const uint8_t *data,
                                 size_t len,
                                 solar_os_vt100_emit_fn emit,
                                 void *user)
{
    size_t consumed = 0;

    if (input == NULL || (data == NULL && len > 0)) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        if (!solar_os_vt100_input_feed_byte(input, data[i], emit, user)) {
            break;
        }
        consumed++;
    }
    return consumed;
}

bool solar_os_vt100_input_flush(solar_os_vt100_input_t *input,
                                solar_os_vt100_emit_fn emit,
                                void *user)
{
    if (input == NULL || input->state == SOLAR_OS_VT100_STATE_NORMAL) {
        return true;
    }

    vt100_sequence_reset(input);
    return vt100_emit(emit, user, (char)SOLAR_OS_KEY_ESCAPE);
}
