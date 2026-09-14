#include "solar_os_agent_reference.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "solar_os_manual.h"

#define AGENT_REFERENCE_MATCH_MAX 3U

static const char *const AGENT_REFERENCE_GUIDANCE =
    "Mandatory SolarOS coding rules: use only symbols and constants documented "
    "in these focused, firmware-matched manual excerpts. Never replace color, "
    "font, key, GPIO mode, or "
    "other constants with guessed strings or numbers. Never invent device, "
    "display, bus, or GPIO names. Treat optional modules as package-gated. "
    "Follow begin/end and cleanup patterns even on errors. Python and Lua "
    "service tables are mirrored; a counterpart-language excerpt preserves "
    "the service contract, but calls must use the requested language's table, "
    "nil/None, and error conventions. If a needed API is not documented here, "
    "call solaros_reference again before writing code.";

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} agent_reference_output_t;

static esp_err_t agent_reference_append(agent_reference_output_t *output,
                                        const char *format,
                                        ...)
{
    if (output == NULL || format == NULL || output->length >= output->capacity) {
        return ESP_ERR_INVALID_ARG;
    }
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(output->buffer + output->length,
                                  output->capacity - output->length,
                                  format,
                                  args);
    va_end(args);
    if (written < 0 || (size_t)written >= output->capacity - output->length) {
        return ESP_ERR_INVALID_SIZE;
    }
    output->length += (size_t)written;
    return ESP_OK;
}

static esp_err_t agent_reference_append_json_string_n(
    agent_reference_output_t *output,
    const char *text,
    size_t text_len)
{
    esp_err_t err = agent_reference_append(output, "\"");
    for (size_t offset = 0U; err == ESP_OK && offset < text_len; offset++) {
        const unsigned char current = (const unsigned char)text[offset];
        char escaped[7];
        const char *value = escaped;
        size_t value_len = 0U;
        switch (current) {
        case '"':
            value = "\\\"";
            value_len = 2U;
            break;
        case '\\':
            value = "\\\\";
            value_len = 2U;
            break;
        case '\b':
            value = "\\b";
            value_len = 2U;
            break;
        case '\f':
            value = "\\f";
            value_len = 2U;
            break;
        case '\n':
            value = "\\n";
            value_len = 2U;
            break;
        case '\r':
            value = "\\r";
            value_len = 2U;
            break;
        case '\t':
            value = "\\t";
            value_len = 2U;
            break;
        default:
            if (current < 0x20U) {
                const int written =
                    snprintf(escaped, sizeof(escaped), "\\u%04x", current);
                if (written != 6) {
                    return ESP_FAIL;
                }
                value_len = 6U;
            } else {
                escaped[0] = (char)current;
                value_len = 1U;
            }
            break;
        }
        if (output->length + value_len >= output->capacity) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(output->buffer + output->length, value, value_len);
        output->length += value_len;
        output->buffer[output->length] = '\0';
    }
    return err == ESP_OK ? agent_reference_append(output, "\"") : err;
}

static esp_err_t agent_reference_append_json_string(
    agent_reference_output_t *output,
    const char *text)
{
    return text != NULL ?
        agent_reference_append_json_string_n(output, text, strlen(text)) :
        ESP_ERR_INVALID_ARG;
}

esp_err_t solar_os_agent_reference_search(const char *query,
                                          char *result,
                                          size_t result_len)
{
    if (query == NULL || query[0] == '\0' || result == NULL ||
        result_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const solar_os_manual_reference_t
        *references[AGENT_REFERENCE_MATCH_MAX] = {0};
    const size_t reference_count =
        solar_os_manual_reference_search(query,
                                         references,
                                         AGENT_REFERENCE_MATCH_MAX);
    const solar_os_manual_page_t *pages[AGENT_REFERENCE_MATCH_MAX] = {0};
    size_t page_count = 0U;
    if (reference_count == 0U) {
        page_count = solar_os_manual_search(query,
                                            pages,
                                            AGENT_REFERENCE_MATCH_MAX);
        if (page_count == 0U) {
            const solar_os_manual_page_t *overview =
                solar_os_manual_find("overview");
            if (overview != NULL) {
                pages[0] = overview;
                page_count = 1U;
            }
        }
    }
    const size_t count = reference_count > 0U ? reference_count : page_count;

    agent_reference_output_t output = {
        .buffer = result,
        .capacity = result_len,
    };
    result[0] = '\0';
    esp_err_t err = agent_reference_append(&output, "{\"guidance\":");
    if (err == ESP_OK) {
        err = agent_reference_append_json_string(&output,
                                                 AGENT_REFERENCE_GUIDANCE);
    }
    if (err == ESP_OK) {
        err = agent_reference_append(&output,
                                     ",\"count\":%u,\"matches\":[",
                                     (unsigned)count);
    }
    for (size_t i = 0U;
         err == ESP_OK && i < reference_count;
         i++) {
        const solar_os_manual_reference_t *reference = references[i];
        const char *text = NULL;
        size_t text_len = 0U;
        err = solar_os_manual_reference_text(reference, &text, &text_len);
        if (err == ESP_OK) {
            err = agent_reference_append(&output,
                                         "%s{\"topic\":",
                                         i == 0U ? "" : ",");
        }
        if (err == ESP_OK) {
            err = agent_reference_append_json_string(&output, reference->topic);
        }
        if (err == ESP_OK) {
            err = agent_reference_append(&output, ",\"section\":");
        }
        if (err == ESP_OK) {
            err = agent_reference_append_json_string(&output,
                                                     reference->section);
        }
        if (err == ESP_OK) {
            err = agent_reference_append(&output,
                                         ",\"part\":%u,\"parts\":%u,"
                                         "\"reference\":",
                                         (unsigned)reference->part,
                                         (unsigned)reference->parts);
        }
        if (err == ESP_OK) {
            err = agent_reference_append_json_string_n(&output, text, text_len);
        }
        if (err == ESP_OK) {
            err = agent_reference_append(&output, "}");
        }
    }
    for (size_t i = 0U; err == ESP_OK && i < page_count; i++) {
        const solar_os_manual_page_t *page = pages[i];
        const char *reference = NULL;
        size_t reference_len = 0U;
        bool reference_owned = false;
        err = solar_os_manual_load_contract(page,
                                            &reference,
                                            &reference_len,
                                            &reference_owned);
        (void)reference_len;
        if (err != ESP_OK) {
            break;
        }
        err = agent_reference_append(&output,
                                     "%s{\"topic\":",
                                     i == 0U ? "" : ",");
        if (err == ESP_OK) {
            err = agent_reference_append_json_string(&output, page->id);
        }
        if (err == ESP_OK) {
            err = agent_reference_append(&output, ",\"reference\":");
        }
        if (err == ESP_OK) {
            err = agent_reference_append_json_string(&output,
                                                     reference);
        }
        if (err == ESP_OK) {
            err = agent_reference_append(&output, "}");
        }
        solar_os_manual_release_text(reference, reference_owned);
    }
    if (err == ESP_OK) {
        err = agent_reference_append(&output, "]}");
    }
    return err;
}
