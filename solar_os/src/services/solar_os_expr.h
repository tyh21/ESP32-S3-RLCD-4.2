#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_EXPR_NODE_MAX 64U
#define SOLAR_OS_EXPR_NAME_MAX 16U
#define SOLAR_OS_EXPR_ERROR_MAX 64U
#define SOLAR_OS_EXPR_ARG_MAX 2U

typedef enum {
    SOLAR_OS_EXPR_NODE_NUMBER,
    SOLAR_OS_EXPR_NODE_VARIABLE,
    SOLAR_OS_EXPR_NODE_UNARY,
    SOLAR_OS_EXPR_NODE_BINARY,
    SOLAR_OS_EXPR_NODE_CALL,
} solar_os_expr_node_kind_t;

typedef struct {
    solar_os_expr_node_kind_t kind;
    union {
        double number;
        char name[SOLAR_OS_EXPR_NAME_MAX];
        struct {
            char op;
            uint8_t child;
        } unary;
        struct {
            char op;
            uint8_t left;
            uint8_t right;
        } binary;
        struct {
            char name[SOLAR_OS_EXPR_NAME_MAX];
            uint8_t args[SOLAR_OS_EXPR_ARG_MAX];
            uint8_t argc;
        } call;
    } value;
} solar_os_expr_node_t;

typedef struct {
    solar_os_expr_node_t nodes[SOLAR_OS_EXPR_NODE_MAX];
    uint8_t node_count;
    uint8_t root;
    bool uses_x;
} solar_os_expr_program_t;

typedef bool (*solar_os_expr_variable_fn)(void *user, const char *name,
                                          double *value);
typedef bool (*solar_os_expr_function_fn)(void *user, const char *name,
                                          const double *args, size_t argc,
                                          double *value);

typedef struct {
    void *user;
    solar_os_expr_variable_fn variable;
    solar_os_expr_function_fn function;
    bool has_x;
    double x;
} solar_os_expr_context_t;

typedef struct {
    size_t position;
    char message[SOLAR_OS_EXPR_ERROR_MAX];
} solar_os_expr_error_t;

esp_err_t solar_os_expr_compile(const char *source,
                                solar_os_expr_program_t *program,
                                solar_os_expr_error_t *error);
esp_err_t solar_os_expr_evaluate(const solar_os_expr_program_t *program,
                                 const solar_os_expr_context_t *context,
                                 double *value, solar_os_expr_error_t *error);
