#include "solar_os_syntax.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>

static bool syntax_has_suffix(const char *path, const char *suffix)
{
    if (path == NULL || suffix == NULL) {
        return false;
    }

    const size_t path_len = strlen(path);
    const size_t suffix_len = strlen(suffix);
    return path_len >= suffix_len &&
        strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

solar_os_syntax_language_t solar_os_syntax_language_for_path(const char *path)
{
    if (syntax_has_suffix(path, ".py") || syntax_has_suffix(path, ".pyw")) {
        return SOLAR_OS_SYNTAX_PYTHON;
    }
    if (syntax_has_suffix(path, ".lua")) {
        return SOLAR_OS_SYNTAX_LUA;
    }
    return SOLAR_OS_SYNTAX_NONE;
}

void solar_os_syntax_state_init(solar_os_syntax_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->mode = SOLAR_OS_SYNTAX_MODE_NORMAL;
    state->lua_long_equals = 0;
}

static void syntax_mark(size_t visible_offset,
                        uint8_t *styles,
                        size_t visible_len,
                        size_t start,
                        size_t end,
                        solar_os_syntax_style_t style)
{
    if (styles == NULL || visible_len == 0 || start >= end) {
        return;
    }

    const size_t visible_end = visible_offset + visible_len;
    if (end <= visible_offset || start >= visible_end) {
        return;
    }

    size_t clipped_start = start > visible_offset ? start : visible_offset;
    size_t clipped_end = end < visible_end ? end : visible_end;
    for (size_t i = clipped_start; i < clipped_end; i++) {
        styles[i - visible_offset] = (uint8_t)style;
    }
}

static bool syntax_ident_start(char ch)
{
    const unsigned char value = (unsigned char)ch;

    return isalpha(value) || ch == '_';
}

static bool syntax_ident(char ch)
{
    const unsigned char value = (unsigned char)ch;

    return isalnum(value) || ch == '_';
}

static bool syntax_keyword_match(const char *word,
                                 size_t len,
                                 const char *const *keywords,
                                 size_t keyword_count)
{
    for (size_t i = 0; i < keyword_count; i++) {
        if (strlen(keywords[i]) == len && strncmp(word, keywords[i], len) == 0) {
            return true;
        }
    }
    return false;
}

static bool python_keyword(const char *word, size_t len)
{
    static const char *const keywords[] = {
        "False", "None", "True", "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else", "except",
        "finally", "for", "from", "global", "if", "import", "in", "is",
        "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
        "while", "with", "yield",
    };

    return syntax_keyword_match(word, len, keywords, sizeof(keywords) / sizeof(keywords[0]));
}

static bool lua_keyword(const char *word, size_t len)
{
    static const char *const keywords[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while",
    };

    return syntax_keyword_match(word, len, keywords, sizeof(keywords) / sizeof(keywords[0]));
}

static size_t syntax_parse_number(const char *line, size_t line_len, size_t pos)
{
    size_t i = pos;

    if (i + 1 < line_len && line[i] == '0' &&
        (line[i + 1] == 'x' || line[i + 1] == 'X' ||
         line[i + 1] == 'b' || line[i + 1] == 'B' ||
         line[i + 1] == 'o' || line[i + 1] == 'O')) {
        i += 2;
    }

    while (i < line_len) {
        const unsigned char ch = (unsigned char)line[i];
        if (isalnum(ch) || ch == '_' || ch == '.') {
            i++;
            continue;
        }
        if ((ch == '+' || ch == '-') && i > pos) {
            const char prev = line[i - 1];
            if (prev == 'e' || prev == 'E' || prev == 'p' || prev == 'P') {
                i++;
                continue;
            }
        }
        break;
    }

    return i > pos ? i : pos + 1;
}

static bool syntax_match_repeated(const char *line,
                                  size_t line_len,
                                  size_t pos,
                                  char ch,
                                  size_t count)
{
    if (pos + count > line_len) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (line[pos + i] != ch) {
            return false;
        }
    }
    return true;
}

static size_t python_find_triple_end(const char *line, size_t line_len, size_t pos, char quote)
{
    while (pos + 2 < line_len) {
        if (syntax_match_repeated(line, line_len, pos, quote, 3)) {
            return pos;
        }
        pos++;
    }
    return line_len;
}

static size_t python_parse_quote_string(const char *line, size_t line_len, size_t pos, char quote)
{
    size_t i = pos + 1;
    bool escaped = false;

    while (i < line_len) {
        const char ch = line[i++];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == quote) {
            break;
        }
    }

    return i;
}

static void python_highlight_line(solar_os_syntax_state_t *state,
                                  const char *line,
                                  size_t line_len,
                                  size_t visible_offset,
                                  uint8_t *styles,
                                  size_t visible_len)
{
    size_t i = 0;

    if (state->mode == SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_SINGLE ||
        state->mode == SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_DOUBLE) {
        const char quote = state->mode == SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_SINGLE ? '\'' : '"';
        const size_t end = python_find_triple_end(line, line_len, 0, quote);
        if (end < line_len) {
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        0,
                        end + 3,
                        SOLAR_OS_SYNTAX_STYLE_STRING);
            state->mode = SOLAR_OS_SYNTAX_MODE_NORMAL;
            i = end + 3;
        } else {
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        0,
                        line_len,
                        SOLAR_OS_SYNTAX_STYLE_STRING);
            return;
        }
    }

    while (i < line_len) {
        const char ch = line[i];

        if (ch == '#') {
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        i,
                        line_len,
                        SOLAR_OS_SYNTAX_STYLE_COMMENT);
            return;
        }

        if (ch == '\'' || ch == '"') {
            const char quote = ch;
            if (syntax_match_repeated(line, line_len, i, quote, 3)) {
                const size_t end = python_find_triple_end(line, line_len, i + 3, quote);
                if (end < line_len) {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                end + 3,
                                SOLAR_OS_SYNTAX_STYLE_STRING);
                    i = end + 3;
                } else {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                line_len,
                                SOLAR_OS_SYNTAX_STYLE_STRING);
                    state->mode = quote == '\'' ?
                        SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_SINGLE :
                        SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_DOUBLE;
                    return;
                }
            } else {
                const size_t end = python_parse_quote_string(line, line_len, i, quote);
                syntax_mark(visible_offset,
                            styles,
                            visible_len,
                            i,
                            end,
                            SOLAR_OS_SYNTAX_STYLE_STRING);
                i = end;
            }
            continue;
        }

        if (isdigit((unsigned char)ch)) {
            const size_t end = syntax_parse_number(line, line_len, i);
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        i,
                        end,
                        SOLAR_OS_SYNTAX_STYLE_NUMBER);
            i = end;
            continue;
        }

        if (syntax_ident_start(ch)) {
            const size_t start = i;
            i++;
            while (i < line_len && syntax_ident(line[i])) {
                i++;
            }
            if (python_keyword(&line[start], i - start)) {
                syntax_mark(visible_offset,
                            styles,
                            visible_len,
                            start,
                            i,
                            SOLAR_OS_SYNTAX_STYLE_KEYWORD);
            }
            continue;
        }

        i++;
    }
}

static bool lua_long_bracket_open(const char *line,
                                  size_t line_len,
                                  size_t pos,
                                  uint8_t *equals)
{
    if (pos >= line_len || line[pos] != '[') {
        return false;
    }

    size_t i = pos + 1;
    while (i < line_len && line[i] == '=') {
        if (i - pos > UINT8_MAX) {
            return false;
        }
        i++;
    }
    if (i >= line_len || line[i] != '[') {
        return false;
    }

    if (equals != NULL) {
        *equals = (uint8_t)(i - pos - 1);
    }
    return true;
}

static bool lua_long_bracket_close_at(const char *line,
                                      size_t line_len,
                                      size_t pos,
                                      uint8_t equals)
{
    if (pos >= line_len || line[pos] != ']') {
        return false;
    }

    size_t i = pos + 1;
    for (uint8_t eq = 0; eq < equals; eq++) {
        if (i >= line_len || line[i] != '=') {
            return false;
        }
        i++;
    }
    return i < line_len && line[i] == ']';
}

static size_t lua_long_bracket_end_len(uint8_t equals)
{
    return (size_t)equals + 2;
}

static size_t lua_find_long_bracket_end(const char *line,
                                        size_t line_len,
                                        size_t pos,
                                        uint8_t equals)
{
    while (pos < line_len) {
        if (lua_long_bracket_close_at(line, line_len, pos, equals)) {
            return pos;
        }
        pos++;
    }
    return line_len;
}

static size_t lua_parse_quote_string(const char *line, size_t line_len, size_t pos, char quote)
{
    return python_parse_quote_string(line, line_len, pos, quote);
}

static bool lua_continue_long(solar_os_syntax_state_t *state,
                              const char *line,
                              size_t line_len,
                              size_t visible_offset,
                              uint8_t *styles,
                              size_t visible_len,
                              size_t *pos)
{
    if (state->mode != SOLAR_OS_SYNTAX_MODE_LUA_LONG_STRING &&
        state->mode != SOLAR_OS_SYNTAX_MODE_LUA_LONG_COMMENT) {
        return false;
    }

    const size_t end = lua_find_long_bracket_end(line, line_len, 0, state->lua_long_equals);
    const solar_os_syntax_style_t style =
        state->mode == SOLAR_OS_SYNTAX_MODE_LUA_LONG_COMMENT ?
        SOLAR_OS_SYNTAX_STYLE_COMMENT :
        SOLAR_OS_SYNTAX_STYLE_STRING;
    if (end < line_len) {
        const size_t close_len = lua_long_bracket_end_len(state->lua_long_equals);
        syntax_mark(visible_offset, styles, visible_len, 0, end + close_len, style);
        state->mode = SOLAR_OS_SYNTAX_MODE_NORMAL;
        state->lua_long_equals = 0;
        *pos = end + close_len;
    } else {
        syntax_mark(visible_offset, styles, visible_len, 0, line_len, style);
        *pos = line_len;
    }
    return true;
}

static void lua_highlight_line(solar_os_syntax_state_t *state,
                               const char *line,
                               size_t line_len,
                               size_t visible_offset,
                               uint8_t *styles,
                               size_t visible_len)
{
    size_t i = 0;

    if (lua_continue_long(state, line, line_len, visible_offset, styles, visible_len, &i) &&
        state->mode != SOLAR_OS_SYNTAX_MODE_NORMAL) {
        return;
    }

    while (i < line_len) {
        const char ch = line[i];

        if (ch == '-' && i + 1 < line_len && line[i + 1] == '-') {
            uint8_t equals = 0;
            if (lua_long_bracket_open(line, line_len, i + 2, &equals)) {
                const size_t open_len = (size_t)equals + 4;
                const size_t end = lua_find_long_bracket_end(line, line_len, i + open_len, equals);
                if (end < line_len) {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                end + lua_long_bracket_end_len(equals),
                                SOLAR_OS_SYNTAX_STYLE_COMMENT);
                    i = end + lua_long_bracket_end_len(equals);
                } else {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                line_len,
                                SOLAR_OS_SYNTAX_STYLE_COMMENT);
                    state->mode = SOLAR_OS_SYNTAX_MODE_LUA_LONG_COMMENT;
                    state->lua_long_equals = equals;
                    return;
                }
            } else {
                syntax_mark(visible_offset,
                            styles,
                            visible_len,
                            i,
                            line_len,
                            SOLAR_OS_SYNTAX_STYLE_COMMENT);
                return;
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            const size_t end = lua_parse_quote_string(line, line_len, i, ch);
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        i,
                        end,
                        SOLAR_OS_SYNTAX_STYLE_STRING);
            i = end;
            continue;
        }

        if (ch == '[') {
            uint8_t equals = 0;
            if (lua_long_bracket_open(line, line_len, i, &equals)) {
                const size_t open_len = (size_t)equals + 2;
                const size_t end = lua_find_long_bracket_end(line, line_len, i + open_len, equals);
                if (end < line_len) {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                end + lua_long_bracket_end_len(equals),
                                SOLAR_OS_SYNTAX_STYLE_STRING);
                    i = end + lua_long_bracket_end_len(equals);
                } else {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                line_len,
                                SOLAR_OS_SYNTAX_STYLE_STRING);
                    state->mode = SOLAR_OS_SYNTAX_MODE_LUA_LONG_STRING;
                    state->lua_long_equals = equals;
                    return;
                }
                continue;
            }
        }

        if (isdigit((unsigned char)ch)) {
            const size_t end = syntax_parse_number(line, line_len, i);
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        i,
                        end,
                        SOLAR_OS_SYNTAX_STYLE_NUMBER);
            i = end;
            continue;
        }

        if (syntax_ident_start(ch)) {
            const size_t start = i;
            i++;
            while (i < line_len && syntax_ident(line[i])) {
                i++;
            }
            if (lua_keyword(&line[start], i - start)) {
                syntax_mark(visible_offset,
                            styles,
                            visible_len,
                            start,
                            i,
                            SOLAR_OS_SYNTAX_STYLE_KEYWORD);
            }
            continue;
        }

        i++;
    }
}

void solar_os_syntax_highlight_line(solar_os_syntax_language_t language,
                                    solar_os_syntax_state_t *state,
                                    const char *line,
                                    size_t line_len,
                                    size_t visible_offset,
                                    uint8_t *styles,
                                    size_t visible_len)
{
    if (styles != NULL) {
        memset(styles, SOLAR_OS_SYNTAX_STYLE_NORMAL, visible_len);
    }
    if (state == NULL || line == NULL || language == SOLAR_OS_SYNTAX_NONE) {
        return;
    }

    switch (language) {
    case SOLAR_OS_SYNTAX_PYTHON:
        python_highlight_line(state, line, line_len, visible_offset, styles, visible_len);
        break;
    case SOLAR_OS_SYNTAX_LUA:
        lua_highlight_line(state, line, line_len, visible_offset, styles, visible_len);
        break;
    case SOLAR_OS_SYNTAX_NONE:
    default:
        break;
    }
}
