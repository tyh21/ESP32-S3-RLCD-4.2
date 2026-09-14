#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLAR_OS_XML_NAME_MAX 64
#define SOLAR_OS_XML_ATTR_VALUE_MAX 256
#define SOLAR_OS_XML_MAX_ATTRS 16

typedef enum {
    SOLAR_OS_XML_EVENT_START,
    SOLAR_OS_XML_EVENT_END,
    SOLAR_OS_XML_EVENT_TEXT,
    SOLAR_OS_XML_EVENT_CDATA,
    SOLAR_OS_XML_EVENT_COMMENT,
    SOLAR_OS_XML_EVENT_PI,
} solar_os_xml_event_type_t;

typedef struct {
    char name[SOLAR_OS_XML_NAME_MAX];
    char value[SOLAR_OS_XML_ATTR_VALUE_MAX];
} solar_os_xml_attr_t;

typedef struct {
    solar_os_xml_event_type_t type;
    const char *name;
    const solar_os_xml_attr_t *attrs;
    size_t attr_count;
    const char *text;
    size_t text_len;
    bool self_closing;
} solar_os_xml_event_t;

typedef esp_err_t (*solar_os_xml_event_cb_t)(const solar_os_xml_event_t *event, void *user);

esp_err_t solar_os_xml_parse(const char *source,
                             size_t source_len,
                             solar_os_xml_event_cb_t callback,
                             void *user);
const char *solar_os_xml_local_name(const char *name);
bool solar_os_xml_name_eq(const char *name, const char *local_name);
const char *solar_os_xml_attr(const solar_os_xml_event_t *event, const char *name);
esp_err_t solar_os_xml_decode_entities(const char *source,
                                       size_t source_len,
                                       char *out,
                                       size_t out_len);

#ifdef __cplusplus
}
#endif
