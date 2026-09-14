#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "solar_os_doc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct solar_os_epub_book solar_os_epub_book_t;

esp_err_t solar_os_epub_open(const char *path, solar_os_epub_book_t **out_book);
void solar_os_epub_close(solar_os_epub_book_t *book);
size_t solar_os_epub_spine_count(const solar_os_epub_book_t *book);
const char *solar_os_epub_title(const solar_os_epub_book_t *book);
esp_err_t solar_os_epub_load_spine_doc(solar_os_epub_book_t *book,
                                       size_t spine_index,
                                       solar_os_doc_t *doc);
esp_err_t solar_os_epub_load_doc(solar_os_doc_t *doc, const char *path);
esp_err_t solar_os_epub_asset_read(void *user,
                                   const char *document_path,
                                   const char *target,
                                   uint8_t **out_data,
                                   size_t *out_len);
void solar_os_epub_asset_release(void *user, uint8_t *data);

#ifdef __cplusplus
}
#endif
