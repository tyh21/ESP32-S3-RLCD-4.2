#include "solar_os_keys.h"

#include <stddef.h>
#include <string.h>
#include <strings.h>

typedef struct {
    const char *name;
    uint8_t key;
} key_name_t;

static const key_name_t key_names[] = {
    {"UP", SOLAR_OS_KEY_UP},
    {"DOWN", SOLAR_OS_KEY_DOWN},
    {"LEFT", SOLAR_OS_KEY_LEFT},
    {"RIGHT", SOLAR_OS_KEY_RIGHT},
    {"PAGE_UP", SOLAR_OS_KEY_PAGE_UP},
    {"PAGE_DOWN", SOLAR_OS_KEY_PAGE_DOWN},
    {"F1", SOLAR_OS_KEY_F1},
    {"F2", SOLAR_OS_KEY_F2},
    {"F3", SOLAR_OS_KEY_F3},
    {"F4", SOLAR_OS_KEY_F4},
    {"F5", SOLAR_OS_KEY_F5},
    {"F6", SOLAR_OS_KEY_F6},
    {"F7", SOLAR_OS_KEY_F7},
    {"F8", SOLAR_OS_KEY_F8},
    {"F9", SOLAR_OS_KEY_F9},
    {"F10", SOLAR_OS_KEY_F10},
    {"F11", SOLAR_OS_KEY_F11},
    {"F12", SOLAR_OS_KEY_F12},
    {"APP_EXIT", SOLAR_OS_KEY_APP_EXIT},
    {"ALT_PREFIX", SOLAR_OS_KEY_ALT_PREFIX},
    {"HOME", SOLAR_OS_KEY_HOME},
    {"END", SOLAR_OS_KEY_END},
    {"DELETE", SOLAR_OS_KEY_DELETE},
    {"SHIFT_UP", SOLAR_OS_KEY_SHIFT_UP},
    {"SHIFT_DOWN", SOLAR_OS_KEY_SHIFT_DOWN},
    {"SHIFT_LEFT", SOLAR_OS_KEY_SHIFT_LEFT},
    {"SHIFT_RIGHT", SOLAR_OS_KEY_SHIFT_RIGHT},
    {"SHIFT_PAGE_UP", SOLAR_OS_KEY_SHIFT_PAGE_UP},
    {"SHIFT_PAGE_DOWN", SOLAR_OS_KEY_SHIFT_PAGE_DOWN},
    {"SHIFT_HOME", SOLAR_OS_KEY_SHIFT_HOME},
    {"SHIFT_END", SOLAR_OS_KEY_SHIFT_END},
    {"CTRL_UP", SOLAR_OS_KEY_CTRL_UP},
    {"CTRL_DOWN", SOLAR_OS_KEY_CTRL_DOWN},
    {"CTRL_LEFT", SOLAR_OS_KEY_CTRL_LEFT},
    {"CTRL_RIGHT", SOLAR_OS_KEY_CTRL_RIGHT},
    {"CTRL_SHIFT_UP", SOLAR_OS_KEY_CTRL_SHIFT_UP},
    {"CTRL_SHIFT_DOWN", SOLAR_OS_KEY_CTRL_SHIFT_DOWN},
    {"CTRL_SHIFT_LEFT", SOLAR_OS_KEY_CTRL_SHIFT_LEFT},
    {"CTRL_SHIFT_RIGHT", SOLAR_OS_KEY_CTRL_SHIFT_RIGHT},
    {"CTRL_HOME", SOLAR_OS_KEY_CTRL_HOME},
    {"CTRL_END", SOLAR_OS_KEY_CTRL_END},
    {"CTRL_SHIFT_HOME", SOLAR_OS_KEY_CTRL_SHIFT_HOME},
    {"CTRL_SHIFT_END", SOLAR_OS_KEY_CTRL_SHIFT_END},
    {"CTRL_PLUS", SOLAR_OS_KEY_CTRL_PLUS},
    {"CTRL_MINUS", SOLAR_OS_KEY_CTRL_MINUS},
    {"CTRL", SOLAR_OS_KEY_CTRL},
    {"AUDIO_MUTE", SOLAR_OS_KEY_AUDIO_MUTE_TOGGLE},
    {"ESCAPE", SOLAR_OS_KEY_ESCAPE},
    {"ENTER", SOLAR_OS_KEY_ENTER},
    {"TAB", '\t'},
    {"BACKSPACE", '\b'},
    {"SPACE", ' '},
};

typedef struct {
    const char *alias;
    const char *canonical;
} key_alias_t;

static const key_alias_t key_aliases[] = {
    {"ARROW_UP", "UP"},
    {"ARROW_DOWN", "DOWN"},
    {"ARROW_LEFT", "LEFT"},
    {"ARROW_RIGHT", "RIGHT"},
    {"PGUP", "PAGE_UP"},
    {"PGDN", "PAGE_DOWN"},
    {"RETURN", "ENTER"},
    {"ESC", "ESCAPE"},
    {"MUTE", "AUDIO_MUTE"},
};

static const key_name_t *find_name(const char *name)
{
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
        if (strcasecmp(key_names[i].name, name) == 0) {
            return &key_names[i];
        }
    }
    return NULL;
}

bool solar_os_key_parse(const char *text, uint8_t *key)
{
    if (text == NULL || text[0] == '\0' || key == NULL) {
        return false;
    }
    if (text[1] == '\0') {
        *key = (uint8_t)text[0];
        return true;
    }

    const key_name_t *named = find_name(text);
    if (named != NULL) {
        *key = named->key;
        return true;
    }
    for (size_t i = 0; i < sizeof(key_aliases) / sizeof(key_aliases[0]); i++) {
        if (strcasecmp(key_aliases[i].alias, text) == 0) {
            named = find_name(key_aliases[i].canonical);
            if (named != NULL) {
                *key = named->key;
                return true;
            }
        }
    }
    return false;
}

const char *solar_os_key_name(uint8_t key)
{
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
        if (key_names[i].key == key) {
            return key_names[i].name;
        }
    }
    return NULL;
}
