#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "solar_os_doc.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t solar_os_html_parse_doc(solar_os_doc_t *doc,
                                  const char *source,
                                  size_t source_len,
                                  const char *path);
esp_err_t solar_os_html_to_markdown(const char *source,
                                    size_t source_len,
                                    const char *asset_base,
                                    char **out_markdown,
                                    size_t *out_len);
void solar_os_html_free(char *markdown);

#ifdef __cplusplus
}
#endif
