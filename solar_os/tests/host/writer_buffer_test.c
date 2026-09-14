#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "solar_os_writer_buffer.h"

static void expect_text(const solar_os_writer_buffer_t *buffer, const char *expected, size_t len)
{
    char *flat = NULL;
    size_t flat_len = 0;
    assert(solar_os_writer_buffer_flatten(buffer, &flat, &flat_len) == ESP_OK);
    assert(flat_len == len);
    assert(memcmp(flat, expected, len) == 0);
    free(flat);
}

static void reference_replace(char *reference,
                              size_t *len,
                              size_t offset,
                              size_t remove_len,
                              const char *insert,
                              size_t insert_len)
{
    memmove(&reference[offset + insert_len],
            &reference[offset + remove_len],
            *len - offset - remove_len);
    memcpy(&reference[offset], insert, insert_len);
    *len = *len - remove_len + insert_len;
}

static void test_growth_and_selection(void)
{
    solar_os_writer_buffer_t buffer;
    solar_os_writer_buffer_init(&buffer);

    const size_t len = SOLAR_OS_WRITER_BUFFER_INITIAL_BYTES + 8192U;
    char *text = malloc(len);
    assert(text != NULL);
    for (size_t i = 0; i < len; i++) {
        text[i] = (char)('a' + (i % 26U));
    }
    assert(solar_os_writer_buffer_set(&buffer, text, len) == ESP_OK);
    assert(buffer.capacity == SOLAR_OS_WRITER_BUFFER_INITIAL_BYTES * 2U);

    const char replacement[] = "selected";
    assert(solar_os_writer_buffer_replace(&buffer,
                                           1024,
                                           4096,
                                           replacement,
                                           sizeof(replacement) - 1U,
                                           5120,
                                           1024 + sizeof(replacement) - 1U) == ESP_OK);
    reference_replace(text,
                      &(size_t){len},
                      1024,
                      4096,
                      replacement,
                      sizeof(replacement) - 1U);
    char *flat = NULL;
    size_t flat_len = 0;
    assert(solar_os_writer_buffer_flatten(&buffer, &flat, &flat_len) == ESP_OK);
    assert(flat_len == len - 4096 + sizeof(replacement) - 1U);
    assert(memcmp(&flat[1024], replacement, sizeof(replacement) - 1U) == 0);
    free(flat);

    assert(solar_os_writer_buffer_set(&buffer, NULL, SOLAR_OS_WRITER_BUFFER_MAX_BYTES + 1U) ==
           ESP_ERR_INVALID_ARG);
    char *oversized = malloc(SOLAR_OS_WRITER_BUFFER_MAX_BYTES + 1U);
    assert(oversized != NULL);
    assert(solar_os_writer_buffer_set(&buffer,
                                      oversized,
                                      SOLAR_OS_WRITER_BUFFER_MAX_BYTES + 1U) ==
           ESP_ERR_INVALID_SIZE);
    free(oversized);
    solar_os_writer_buffer_free(&buffer);
    free(text);
}

static void test_utf8(void)
{
    static const char text[] = "AäéЖשZ";
    static const size_t offsets[] = {0, 1, 3, 5, 7, 9, 10};
    solar_os_writer_buffer_t buffer;
    solar_os_writer_buffer_init(&buffer);
    assert(solar_os_writer_buffer_set(&buffer, text, sizeof(text) - 1U) == ESP_OK);

    size_t pos = 0;
    for (size_t i = 1; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        pos = solar_os_writer_utf8_next(&buffer, pos);
        assert(pos == offsets[i]);
    }
    for (size_t i = sizeof(offsets) / sizeof(offsets[0]) - 1U; i > 0; i--) {
        pos = solar_os_writer_utf8_prev(&buffer, pos);
        assert(pos == offsets[i - 1U]);
    }
    solar_os_writer_buffer_free(&buffer);
}

static void test_random_edits_and_history(void)
{
    solar_os_writer_buffer_t buffer;
    solar_os_writer_buffer_init(&buffer);
    assert(solar_os_writer_buffer_set(&buffer, "seed", 4) == ESP_OK);

    char reference[8192] = "seed";
    size_t reference_len = 4;
    srand(0x534f4c41U);
    for (size_t i = 0; i < 80; i++) {
        const size_t offset = (size_t)rand() % (reference_len + 1U);
        size_t remove_len = reference_len > offset ? (size_t)rand() % 5U : 0;
        if (remove_len > reference_len - offset) {
            remove_len = reference_len - offset;
        }
        char insert[8];
        const size_t insert_len = (size_t)rand() % sizeof(insert);
        for (size_t j = 0; j < insert_len; j++) {
            insert[j] = (char)('a' + (rand() % 26));
        }
        assert(solar_os_writer_buffer_replace(&buffer,
                                               offset,
                                               remove_len,
                                               insert,
                                               insert_len,
                                               offset,
                                               offset + insert_len) == ESP_OK);
        reference_replace(reference,
                          &reference_len,
                          offset,
                          remove_len,
                          insert,
                          insert_len);
        expect_text(&buffer, reference, reference_len);
    }

    char final[8192];
    memcpy(final, reference, reference_len);
    const size_t final_len = reference_len;
    size_t cursor = 0;
    while (solar_os_writer_buffer_undo(&buffer, &cursor)) {
    }
    expect_text(&buffer, "seed", 4);
    while (solar_os_writer_buffer_redo(&buffer, &cursor)) {
    }
    expect_text(&buffer, final, final_len);
    solar_os_writer_buffer_free(&buffer);
}

static void test_history_budgets_and_coalescing(void)
{
    solar_os_writer_buffer_t buffer;
    solar_os_writer_buffer_init(&buffer);
    assert(solar_os_writer_buffer_set(&buffer, NULL, 0) == ESP_OK);

    for (size_t i = 0; i < 32; i++) {
        const char ch = (char)('a' + (i % 26U));
        assert(solar_os_writer_buffer_replace(&buffer, i, 0, &ch, 1, i, i + 1U) == ESP_OK);
    }
    assert(buffer.undo_count == 1);
    assert(buffer.undo_payload == 32);

    solar_os_writer_buffer_clear_undo(&buffer);
    char payload[1024];
    memset(payload, 'x', sizeof(payload));
    for (size_t i = 0; i < 100; i++) {
        assert(solar_os_writer_buffer_replace(&buffer,
                                               0,
                                               0,
                                               payload,
                                               sizeof(payload),
                                               0,
                                               sizeof(payload)) == ESP_OK);
    }
    assert(buffer.undo_count <= SOLAR_OS_WRITER_UNDO_MAX_OPS);
    assert(buffer.undo_payload <= SOLAR_OS_WRITER_UNDO_MAX_PAYLOAD);
    solar_os_writer_buffer_free(&buffer);
}

int main(void)
{
    test_growth_and_selection();
    test_utf8();
    test_random_edits_and_history();
    test_history_budgets_and_coalescing();
    puts("writer buffer tests: ok");
    return 0;
}
