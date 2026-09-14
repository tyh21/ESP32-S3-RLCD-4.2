#include "solar_os_writer_buffer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifndef SOLAR_OS_WRITER_HOST_TEST
#include "solar_os_memory.h"
#endif

static void *writer_alloc(size_t size)
{
#ifdef SOLAR_OS_WRITER_HOST_TEST
    return malloc(size);
#else
    return solar_os_memory_alloc(size,
                                 SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,
                                 "writer.buffer");
#endif
}

static void writer_free(void *ptr)
{
#ifdef SOLAR_OS_WRITER_HOST_TEST
    free(ptr);
#else
    solar_os_memory_free(ptr);
#endif
}

static size_t writer_gap_size(const solar_os_writer_buffer_t *buffer)
{
    return buffer->gap_end - buffer->gap_start;
}

size_t solar_os_writer_buffer_length(const solar_os_writer_buffer_t *buffer)
{
    return buffer != NULL && buffer->data != NULL ?
        buffer->capacity - writer_gap_size(buffer) : 0;
}

void solar_os_writer_buffer_init(solar_os_writer_buffer_t *buffer)
{
    if (buffer != NULL) {
        memset(buffer, 0, sizeof(*buffer));
    }
}

static void writer_op_free(solar_os_writer_undo_op_t *op)
{
    if (op == NULL) {
        return;
    }
    writer_free(op->removed);
    writer_free(op->inserted);
    memset(op, 0, sizeof(*op));
}

void solar_os_writer_buffer_clear_undo(solar_os_writer_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }
    for (size_t i = 0; i < buffer->undo_count; i++) {
        writer_op_free(&buffer->undo[i]);
    }
    buffer->undo_count = 0;
    buffer->undo_cursor = 0;
    buffer->undo_payload = 0;
}

void solar_os_writer_buffer_free(solar_os_writer_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }
    solar_os_writer_buffer_clear_undo(buffer);
    writer_free(buffer->undo);
    writer_free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static esp_err_t writer_reserve(solar_os_writer_buffer_t *buffer, size_t need)
{
    if (buffer == NULL || need > SOLAR_OS_WRITER_BUFFER_MAX_BYTES) {
        return need > SOLAR_OS_WRITER_BUFFER_MAX_BYTES ?
            ESP_ERR_INVALID_SIZE : ESP_ERR_INVALID_ARG;
    }
    if (buffer->data != NULL && buffer->capacity >= need) {
        return ESP_OK;
    }

    size_t capacity = buffer->capacity > 0 ? buffer->capacity :
        SOLAR_OS_WRITER_BUFFER_INITIAL_BYTES;
    while (capacity < need && capacity < SOLAR_OS_WRITER_BUFFER_MAX_BYTES) {
        capacity *= 2U;
    }
    if (capacity > SOLAR_OS_WRITER_BUFFER_MAX_BYTES) {
        capacity = SOLAR_OS_WRITER_BUFFER_MAX_BYTES;
    }
    if (capacity < need) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *next = writer_alloc(capacity);
    if (next == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const size_t suffix_len = buffer->data != NULL ?
        buffer->capacity - buffer->gap_end : 0;
    if (buffer->data != NULL && buffer->gap_start > 0) {
        memcpy(next, buffer->data, buffer->gap_start);
    }
    if (suffix_len > 0) {
        memcpy(&next[capacity - suffix_len],
               &buffer->data[buffer->gap_end],
               suffix_len);
    }
    writer_free(buffer->data);
    buffer->data = next;
    buffer->gap_end = capacity - suffix_len;
    buffer->capacity = capacity;
    return ESP_OK;
}

esp_err_t solar_os_writer_buffer_set(solar_os_writer_buffer_t *buffer,
                                     const char *text,
                                     size_t len)
{
    if (buffer == NULL || (text == NULL && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > SOLAR_OS_WRITER_BUFFER_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    solar_os_writer_buffer_clear_undo(buffer);
    writer_free(buffer->data);
    buffer->data = NULL;
    buffer->capacity = 0;
    buffer->gap_start = 0;
    buffer->gap_end = 0;
    esp_err_t ret = writer_reserve(buffer, len);
    if (ret != ESP_OK) {
        return ret;
    }
    if (len > 0) {
        memcpy(buffer->data, text, len);
    }
    buffer->gap_start = len;
    buffer->gap_end = buffer->capacity;
    return ESP_OK;
}

static char writer_byte_at(const solar_os_writer_buffer_t *buffer, size_t offset)
{
    return offset < buffer->gap_start ? buffer->data[offset] :
        buffer->data[offset + writer_gap_size(buffer)];
}

esp_err_t solar_os_writer_buffer_copy(const solar_os_writer_buffer_t *buffer,
                                      size_t offset,
                                      size_t len,
                                      char *out)
{
    const size_t total = solar_os_writer_buffer_length(buffer);
    if (buffer == NULL || out == NULL || offset > total || len > total - offset) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t prefix = offset < buffer->gap_start ?
        (len < buffer->gap_start - offset ? len : buffer->gap_start - offset) : 0;
    if (prefix > 0) {
        memcpy(out, &buffer->data[offset], prefix);
    }
    if (prefix < len) {
        const size_t logical = offset + prefix;
        memcpy(&out[prefix],
               &buffer->data[logical + writer_gap_size(buffer)],
               len - prefix);
    }
    return ESP_OK;
}

esp_err_t solar_os_writer_buffer_flatten(const solar_os_writer_buffer_t *buffer,
                                         char **out,
                                         size_t *out_len)
{
    if (buffer == NULL || out == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t len = solar_os_writer_buffer_length(buffer);
    char *flat = writer_alloc(len + 1U);
    if (flat == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (len > 0) {
        (void)solar_os_writer_buffer_copy(buffer, 0, len, flat);
    }
    flat[len] = '\0';
    *out = flat;
    *out_len = len;
    return ESP_OK;
}

static void writer_move_gap(solar_os_writer_buffer_t *buffer, size_t offset)
{
    if (offset < buffer->gap_start) {
        const size_t move = buffer->gap_start - offset;
        memmove(&buffer->data[buffer->gap_end - move],
                &buffer->data[offset],
                move);
        buffer->gap_start -= move;
        buffer->gap_end -= move;
    } else if (offset > buffer->gap_start) {
        const size_t move = offset - buffer->gap_start;
        memmove(&buffer->data[buffer->gap_start],
                &buffer->data[buffer->gap_end],
                move);
        buffer->gap_start += move;
        buffer->gap_end += move;
    }
}

static esp_err_t writer_replace_raw(solar_os_writer_buffer_t *buffer,
                                    size_t offset,
                                    size_t remove_len,
                                    const char *insert,
                                    size_t insert_len)
{
    const size_t len = solar_os_writer_buffer_length(buffer);
    if (buffer == NULL || offset > len || remove_len > len - offset ||
        (insert == NULL && insert_len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t next_len = len - remove_len + insert_len;
    esp_err_t ret = writer_reserve(buffer, next_len);
    if (ret != ESP_OK) {
        return ret;
    }
    writer_move_gap(buffer, offset);
    buffer->gap_end += remove_len;
    if (insert_len > 0) {
        memcpy(&buffer->data[buffer->gap_start], insert, insert_len);
        buffer->gap_start += insert_len;
    }
    return ESP_OK;
}

static char *writer_copy_new(const solar_os_writer_buffer_t *buffer,
                             size_t offset,
                             size_t len)
{
    if (len == 0) {
        return NULL;
    }
    char *copy = writer_alloc(len);
    if (copy != NULL && solar_os_writer_buffer_copy(buffer, offset, len, copy) != ESP_OK) {
        writer_free(copy);
        return NULL;
    }
    return copy;
}

static void writer_drop_redo(solar_os_writer_buffer_t *buffer)
{
    for (size_t i = buffer->undo_cursor; i < buffer->undo_count; i++) {
        buffer->undo_payload -= buffer->undo[i].removed_len + buffer->undo[i].inserted_len;
        writer_op_free(&buffer->undo[i]);
    }
    buffer->undo_count = buffer->undo_cursor;
}

static void writer_evict_oldest(solar_os_writer_buffer_t *buffer)
{
    if (buffer->undo_count == 0) {
        return;
    }
    buffer->undo_payload -= buffer->undo[0].removed_len + buffer->undo[0].inserted_len;
    writer_op_free(&buffer->undo[0]);
    memmove(&buffer->undo[0],
            &buffer->undo[1],
            (buffer->undo_count - 1U) * sizeof(buffer->undo[0]));
    buffer->undo_count--;
    if (buffer->undo_cursor > 0) {
        buffer->undo_cursor--;
    }
    memset(&buffer->undo[buffer->undo_count], 0, sizeof(buffer->undo[0]));
}

static bool writer_try_coalesce_insert(solar_os_writer_buffer_t *buffer,
                                       size_t offset,
                                       const char *insert,
                                       size_t insert_len,
                                       size_t cursor_after)
{
    if (buffer->undo_cursor == 0 || buffer->undo_cursor != buffer->undo_count ||
        insert_len == 0) {
        return false;
    }
    solar_os_writer_undo_op_t *op = &buffer->undo[buffer->undo_cursor - 1U];
    if (op->removed_len != 0 || op->offset + op->inserted_len != offset ||
        op->inserted_len + insert_len > SOLAR_OS_WRITER_UNDO_MAX_PAYLOAD) {
        return false;
    }
    char *joined = writer_alloc(op->inserted_len + insert_len);
    if (joined == NULL) {
        return false;
    }
    memcpy(joined, op->inserted, op->inserted_len);
    memcpy(&joined[op->inserted_len], insert, insert_len);
    writer_free(op->inserted);
    op->inserted = joined;
    op->inserted_len += insert_len;
    op->cursor_after = cursor_after;
    buffer->undo_payload += insert_len;
    return true;
}

static bool writer_try_coalesce_delete(solar_os_writer_buffer_t *buffer,
                                       size_t offset,
                                       const char *removed,
                                       size_t removed_len,
                                       size_t cursor_after)
{
    if (buffer->undo == NULL || buffer->undo_cursor == 0 ||
        buffer->undo_cursor != buffer->undo_count || removed_len == 0) {
        return false;
    }
    solar_os_writer_undo_op_t *op = &buffer->undo[buffer->undo_cursor - 1U];
    if (op->inserted_len != 0 ||
        (op->offset != offset && offset + removed_len != op->offset) ||
        op->removed_len + removed_len > SOLAR_OS_WRITER_UNDO_MAX_PAYLOAD) {
        return false;
    }
    char *joined = writer_alloc(op->removed_len + removed_len);
    if (joined == NULL) {
        return false;
    }
    if (op->offset == offset) {
        memcpy(joined, op->removed, op->removed_len);
        memcpy(&joined[op->removed_len], removed, removed_len);
    } else {
        memcpy(joined, removed, removed_len);
        memcpy(&joined[removed_len], op->removed, op->removed_len);
        op->offset = offset;
    }
    writer_free(op->removed);
    op->removed = joined;
    op->removed_len += removed_len;
    op->cursor_after = cursor_after;
    buffer->undo_payload += removed_len;
    return true;
}

static bool writer_ensure_undo_storage(solar_os_writer_buffer_t *buffer)
{
    if (buffer->undo != NULL) {
        return true;
    }
    buffer->undo = writer_alloc(SOLAR_OS_WRITER_UNDO_MAX_OPS * sizeof(buffer->undo[0]));
    if (buffer->undo == NULL) {
        return false;
    }
    memset(buffer->undo, 0, SOLAR_OS_WRITER_UNDO_MAX_OPS * sizeof(buffer->undo[0]));
    buffer->undo_capacity = SOLAR_OS_WRITER_UNDO_MAX_OPS;
    return true;
}

esp_err_t solar_os_writer_buffer_replace(solar_os_writer_buffer_t *buffer,
                                         size_t offset,
                                         size_t remove_len,
                                         const char *insert,
                                         size_t insert_len,
                                         size_t cursor_before,
                                         size_t cursor_after)
{
    const size_t len = solar_os_writer_buffer_length(buffer);
    if (buffer == NULL || offset > len || remove_len > len - offset ||
        (insert == NULL && insert_len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (remove_len == 0 && insert_len == 0) {
        return ESP_OK;
    }

    char *removed = writer_copy_new(buffer, offset, remove_len);
    if (remove_len > 0 && removed == NULL) {
        return ESP_ERR_NO_MEM;
    }
    char *inserted = NULL;
    if (insert_len > 0) {
        inserted = writer_alloc(insert_len);
        if (inserted == NULL) {
            writer_free(removed);
            return ESP_ERR_NO_MEM;
        }
        memcpy(inserted, insert, insert_len);
    }

    esp_err_t ret = writer_replace_raw(buffer, offset, remove_len, insert, insert_len);
    if (ret != ESP_OK) {
        writer_free(removed);
        writer_free(inserted);
        return ret;
    }

    writer_drop_redo(buffer);
    if (!writer_ensure_undo_storage(buffer)) {
        writer_free(removed);
        writer_free(inserted);
        return ESP_OK;
    }
    if (remove_len == 0 &&
        writer_try_coalesce_insert(buffer, offset, insert, insert_len, cursor_after)) {
        writer_free(removed);
        writer_free(inserted);
    } else if (insert_len == 0 &&
               writer_try_coalesce_delete(buffer,
                                          offset,
                                          removed,
                                          remove_len,
                                          cursor_after)) {
        writer_free(removed);
        writer_free(inserted);
    } else {
        const size_t payload = remove_len + insert_len;
        while (buffer->undo_count > 0 &&
               (buffer->undo_count >= SOLAR_OS_WRITER_UNDO_MAX_OPS ||
                buffer->undo_payload + payload > SOLAR_OS_WRITER_UNDO_MAX_PAYLOAD)) {
            writer_evict_oldest(buffer);
        }
        if (payload <= SOLAR_OS_WRITER_UNDO_MAX_PAYLOAD) {
            buffer->undo[buffer->undo_count++] = (solar_os_writer_undo_op_t){
                .offset = offset,
                .removed_len = remove_len,
                .inserted_len = insert_len,
                .removed = removed,
                .inserted = inserted,
                .cursor_before = cursor_before,
                .cursor_after = cursor_after,
            };
            buffer->undo_cursor = buffer->undo_count;
            buffer->undo_payload += payload;
        } else {
            writer_free(removed);
            writer_free(inserted);
        }
    }
    while (buffer->undo_count > 0 &&
           buffer->undo_payload > SOLAR_OS_WRITER_UNDO_MAX_PAYLOAD) {
        writer_evict_oldest(buffer);
    }
    return ESP_OK;
}

bool solar_os_writer_buffer_undo(solar_os_writer_buffer_t *buffer, size_t *cursor)
{
    if (buffer == NULL || buffer->undo_cursor == 0) {
        return false;
    }
    solar_os_writer_undo_op_t *op = &buffer->undo[buffer->undo_cursor - 1U];
    if (writer_replace_raw(buffer,
                           op->offset,
                           op->inserted_len,
                           op->removed,
                           op->removed_len) != ESP_OK) {
        return false;
    }
    buffer->undo_cursor--;
    if (cursor != NULL) {
        *cursor = op->cursor_before;
    }
    return true;
}

bool solar_os_writer_buffer_redo(solar_os_writer_buffer_t *buffer, size_t *cursor)
{
    if (buffer == NULL || buffer->undo_cursor >= buffer->undo_count) {
        return false;
    }
    solar_os_writer_undo_op_t *op = &buffer->undo[buffer->undo_cursor];
    if (writer_replace_raw(buffer,
                           op->offset,
                           op->removed_len,
                           op->inserted,
                           op->inserted_len) != ESP_OK) {
        return false;
    }
    buffer->undo_cursor++;
    if (cursor != NULL) {
        *cursor = op->cursor_after;
    }
    return true;
}

size_t solar_os_writer_utf8_prev(const solar_os_writer_buffer_t *buffer, size_t offset)
{
    const size_t len = solar_os_writer_buffer_length(buffer);
    if (offset > len) {
        offset = len;
    }
    if (offset == 0) {
        return 0;
    }
    offset--;
    while (offset > 0 &&
           (((uint8_t)writer_byte_at(buffer, offset) & 0xc0U) == 0x80U)) {
        offset--;
    }
    return offset;
}

size_t solar_os_writer_utf8_next(const solar_os_writer_buffer_t *buffer, size_t offset)
{
    const size_t len = solar_os_writer_buffer_length(buffer);
    if (offset >= len) {
        return len;
    }
    offset++;
    while (offset < len &&
           (((uint8_t)writer_byte_at(buffer, offset) & 0xc0U) == 0x80U)) {
        offset++;
    }
    return offset;
}

static bool writer_word_byte(char ch)
{
    const uint8_t byte = (uint8_t)ch;
    return byte >= 0x80U || isalnum(byte) || ch == '_';
}

size_t solar_os_writer_word_prev(const solar_os_writer_buffer_t *buffer, size_t offset)
{
    size_t pos = solar_os_writer_utf8_prev(buffer, offset);
    while (pos > 0 && !writer_word_byte(writer_byte_at(buffer, pos))) {
        pos = solar_os_writer_utf8_prev(buffer, pos);
    }
    while (pos > 0) {
        const size_t prev = solar_os_writer_utf8_prev(buffer, pos);
        if (!writer_word_byte(writer_byte_at(buffer, prev))) {
            break;
        }
        pos = prev;
    }
    return pos;
}

size_t solar_os_writer_word_next(const solar_os_writer_buffer_t *buffer, size_t offset)
{
    const size_t len = solar_os_writer_buffer_length(buffer);
    size_t pos = offset > len ? len : offset;
    while (pos < len && writer_word_byte(writer_byte_at(buffer, pos))) {
        pos = solar_os_writer_utf8_next(buffer, pos);
    }
    while (pos < len && !writer_word_byte(writer_byte_at(buffer, pos))) {
        pos = solar_os_writer_utf8_next(buffer, pos);
    }
    return pos;
}
