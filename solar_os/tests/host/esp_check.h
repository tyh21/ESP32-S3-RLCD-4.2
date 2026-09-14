#pragma once

#define ESP_RETURN_ON_ERROR(expression, tag, message) \
    do {                                                \
        (void)(tag);                                    \
        (void)(message);                                \
        const esp_err_t error = (expression);           \
        if (error != ESP_OK) {                          \
            return error;                               \
        }                                               \
    } while (0)
