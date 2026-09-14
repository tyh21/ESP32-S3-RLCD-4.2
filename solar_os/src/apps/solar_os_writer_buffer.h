#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_WRITER_BUFFER_INITIAL_BYTES (64U * 1024U)
#define SOLAR_OS_WRITER_BUFFER_MAX_BYTES (256U * 1024U)
#define SOLAR_OS_WRITER_UNDO_MAX_OPS 128U
#define SOLAR_OS_WRITER_UNDO_MAX_PAYLOAD (64U * 1024U)

typedef struct {
    size_t offset;
    size_t removed_len;
    size_t inserted_len;
    char *removed;
    char *inserted;
    size_t cursor_before;
    size_t cursor_after;
} solar_os_writer_undo_op_t;

typedef struct {
    char *data;
    size_t capacity;
    size_t gap_start;
    size_t gap_end;
    solar_os_writer_undo_op_t *undo;
    size_t undo_capacity;
    size_t undo_count;
    size_t undo_cursor;
    size_t undo_payload;
} solar_os_writer_buffer_t;

void solar_os_writer_buffer_init(solar_os_writer_buffer_t *buffer);
void solar_os_writer_buffer_free(solar_os_writer_buffer_t *buffer);
esp_err_t solar_os_writer_buffer_set(solar_os_writer_buffer_t *buffer,
                                     const char *text,
                                     size_t len);
size_t solar_os_writer_buffer_length(const solar_os_writer_buffer_t *buffer);
esp_err_t solar_os_writer_buffer_flatten(const solar_os_writer_buffer_t *buffer,
                                         char **out,
                                         size_t *out_len);
esp_err_t solar_os_writer_buffer_copy(const solar_os_writer_buffer_t *buffer,
                                      size_t offset,
                                      size_t len,
                                      char *out);
esp_err_t solar_os_writer_buffer_replace(solar_os_writer_buffer_t *buffer,
                                         size_t offset,
                                         size_t remove_len,
                                         const char *insert,
                                         size_t insert_len,
                                         size_t cursor_before,
                                         size_t cursor_after);
bool solar_os_writer_buffer_undo(solar_os_writer_buffer_t *buffer, size_t *cursor);
bool solar_os_writer_buffer_redo(solar_os_writer_buffer_t *buffer, size_t *cursor);
void solar_os_writer_buffer_clear_undo(solar_os_writer_buffer_t *buffer);

size_t solar_os_writer_utf8_prev(const solar_os_writer_buffer_t *buffer, size_t offset);
size_t solar_os_writer_utf8_next(const solar_os_writer_buffer_t *buffer, size_t offset);
size_t solar_os_writer_word_prev(const solar_os_writer_buffer_t *buffer, size_t offset);
size_t solar_os_writer_word_next(const solar_os_writer_buffer_t *buffer, size_t offset);
