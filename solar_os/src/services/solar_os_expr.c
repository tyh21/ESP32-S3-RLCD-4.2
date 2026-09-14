#include "solar_os_expr.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPR_EVAL_DEPTH_MAX 24U

typedef struct {
    const char *source;
    const char *cursor;
    solar_os_expr_program_t *program;
    solar_os_expr_error_t *error;
    bool failed;
} expr_parser_t;

static void expr_error_set(solar_os_expr_error_t *error, const char *source,
                           const char *at, const char *message)
{
    if (error == NULL) {
        return;
    }
    error->position = source != NULL && at != NULL ? (size_t)(at - source) : 0U;
    snprintf(error->message, sizeof(error->message), "%s",
             message != NULL ? message : "error");
}

static void parser_fail(expr_parser_t *parser, const char *message)
{
    if (!parser->failed) {
        expr_error_set(parser->error, parser->source, parser->cursor, message);
        parser->failed = true;
    }
}

static void parser_space(expr_parser_t *parser)
{
    while (isspace((unsigned char)*parser->cursor)) {
        parser->cursor++;
    }
}

static int parser_node(expr_parser_t *parser, const solar_os_expr_node_t *node)
{
    if (parser->program->node_count >= SOLAR_OS_EXPR_NODE_MAX) {
        parser_fail(parser, "expression is too complex");
        return -1;
    }
    const uint8_t index = parser->program->node_count++;
    parser->program->nodes[index] = *node;
    return (int)index;
}

static int parser_expression(expr_parser_t *parser);

static bool parser_identifier(expr_parser_t *parser, char *name,
                              size_t name_len)
{
    parser_space(parser);
    if (!(isalpha((unsigned char)*parser->cursor) || *parser->cursor == '_')) {
        return false;
    }
    size_t used = 0U;
    while (isalnum((unsigned char)*parser->cursor) || *parser->cursor == '_') {
        if (used + 1U >= name_len) {
            parser_fail(parser, "name is too long");
            return false;
        }
        name[used++] = (char)tolower((unsigned char)*parser->cursor++);
    }
    name[used] = '\0';
    return true;
}

static int parser_primary(expr_parser_t *parser)
{
    parser_space(parser);
    if (*parser->cursor == '(') {
        parser->cursor++;
        const int child = parser_expression(parser);
        parser_space(parser);
        if (*parser->cursor != ')') {
            parser_fail(parser, "expected ')'");
            return -1;
        }
        parser->cursor++;
        return child;
    }

    if (isdigit((unsigned char)*parser->cursor) || *parser->cursor == '.') {
        char *end = NULL;
        const double value = strtod(parser->cursor, &end);
        if (end == parser->cursor) {
            parser_fail(parser, "invalid number");
            return -1;
        }
        parser->cursor = end;
        const solar_os_expr_node_t node = {
            .kind = SOLAR_OS_EXPR_NODE_NUMBER,
            .value.number = value,
        };
        return parser_node(parser, &node);
    }

    char name[SOLAR_OS_EXPR_NAME_MAX];
    if (!parser_identifier(parser, name, sizeof(name))) {
        if (!parser->failed) {
            parser_fail(parser, "expected a number, name, or '('");
        }
        return -1;
    }
    parser_space(parser);
    if (*parser->cursor != '(') {
        solar_os_expr_node_t node = {.kind = SOLAR_OS_EXPR_NODE_VARIABLE};
        snprintf(node.value.name, sizeof(node.value.name), "%s", name);
        if (strcmp(name, "x") == 0) {
            parser->program->uses_x = true;
        }
        return parser_node(parser, &node);
    }

    parser->cursor++;
    solar_os_expr_node_t node = {.kind = SOLAR_OS_EXPR_NODE_CALL};
    snprintf(node.value.call.name, sizeof(node.value.call.name), "%s", name);
    parser_space(parser);
    if (*parser->cursor != ')') {
        while (true) {
            if (node.value.call.argc >= SOLAR_OS_EXPR_ARG_MAX) {
                parser_fail(parser, "functions accept at most two arguments");
                return -1;
            }
            const int arg = parser_expression(parser);
            if (arg < 0) {
                return -1;
            }
            node.value.call.args[node.value.call.argc++] = (uint8_t)arg;
            parser_space(parser);
            if (*parser->cursor != ',') {
                break;
            }
            parser->cursor++;
        }
    }
    if (*parser->cursor != ')') {
        parser_fail(parser, "expected ')' after arguments");
        return -1;
    }
    parser->cursor++;
    return parser_node(parser, &node);
}

static int parser_power(expr_parser_t *parser);

static int parser_unary(expr_parser_t *parser)
{
    parser_space(parser);
    if (*parser->cursor == '+' || *parser->cursor == '-') {
        const char op = *parser->cursor++;
        const int child = parser_unary(parser);
        if (child < 0) {
            return -1;
        }
        const solar_os_expr_node_t node = {
            .kind = SOLAR_OS_EXPR_NODE_UNARY,
            .value.unary = {.op = op, .child = (uint8_t)child},
        };
        return parser_node(parser, &node);
    }
    return parser_power(parser);
}

static int parser_power(expr_parser_t *parser)
{
    int left = parser_primary(parser);
    if (left < 0) {
        return -1;
    }
    parser_space(parser);
    if (*parser->cursor == '^') {
        parser->cursor++;
        const int right = parser_unary(parser);
        if (right < 0) {
            return -1;
        }
        const solar_os_expr_node_t node = {
            .kind = SOLAR_OS_EXPR_NODE_BINARY,
            .value.binary = {.op = '^',
                             .left = (uint8_t)left,
                             .right = (uint8_t)right},
        };
        return parser_node(parser, &node);
    }
    return left;
}

static bool parser_starts_implicit_factor(expr_parser_t *parser)
{
    parser_space(parser);
    return *parser->cursor == '(' || isalpha((unsigned char)*parser->cursor) ||
           *parser->cursor == '_';
}

static int parser_term(expr_parser_t *parser)
{
    int left = parser_unary(parser);
    while (left >= 0) {
        parser_space(parser);
        char op = *parser->cursor;
        if (op == '*' || op == '/' || op == '%') {
            parser->cursor++;
        } else if (parser_starts_implicit_factor(parser)) {
            op = '*';
        } else {
            break;
        }
        const int right = parser_unary(parser);
        if (right < 0) {
            return -1;
        }
        const solar_os_expr_node_t node = {
            .kind = SOLAR_OS_EXPR_NODE_BINARY,
            .value.binary = {.op = op,
                             .left = (uint8_t)left,
                             .right = (uint8_t)right},
        };
        left = parser_node(parser, &node);
    }
    return left;
}

static int parser_expression(expr_parser_t *parser)
{
    int left = parser_term(parser);
    while (left >= 0) {
        parser_space(parser);
        const char op = *parser->cursor;
        if (op != '+' && op != '-') {
            break;
        }
        parser->cursor++;
        const int right = parser_term(parser);
        if (right < 0) {
            return -1;
        }
        const solar_os_expr_node_t node = {
            .kind = SOLAR_OS_EXPR_NODE_BINARY,
            .value.binary = {.op = op,
                             .left = (uint8_t)left,
                             .right = (uint8_t)right},
        };
        left = parser_node(parser, &node);
    }
    return left;
}

esp_err_t solar_os_expr_compile(const char *source,
                                solar_os_expr_program_t *program,
                                solar_os_expr_error_t *error)
{
    if (source == NULL || program == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(program, 0, sizeof(*program));
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
    }
    expr_parser_t parser = {
        .source = source,
        .cursor = source,
        .program = program,
        .error = error,
    };
    parser_space(&parser);
    if (*parser.cursor == '\0') {
        parser_fail(&parser, "empty expression");
        return ESP_ERR_INVALID_ARG;
    }
    const int root = parser_expression(&parser);
    parser_space(&parser);
    if (!parser.failed && *parser.cursor != '\0') {
        parser_fail(&parser, "unexpected character");
    }
    if (parser.failed || root < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    program->root = (uint8_t)root;
    return ESP_OK;
}

static bool expr_builtin(const char *name, const double *args, size_t argc,
                         double *value)
{
#define EXPR_UNARY(fn)                                                         \
    do {                                                                       \
        if (argc != 1U)                                                        \
            return false;                                                      \
        *value = fn(args[0]);                                                  \
        return true;                                                           \
    } while (0)
    if (strcmp(name, "sin") == 0)
        EXPR_UNARY(sin);
    if (strcmp(name, "cos") == 0)
        EXPR_UNARY(cos);
    if (strcmp(name, "tan") == 0)
        EXPR_UNARY(tan);
    if (strcmp(name, "asin") == 0)
        EXPR_UNARY(asin);
    if (strcmp(name, "acos") == 0)
        EXPR_UNARY(acos);
    if (strcmp(name, "atan") == 0)
        EXPR_UNARY(atan);
    if (strcmp(name, "sqrt") == 0)
        EXPR_UNARY(sqrt);
    if (strcmp(name, "abs") == 0)
        EXPR_UNARY(fabs);
    if (strcmp(name, "exp") == 0)
        EXPR_UNARY(exp);
    if (strcmp(name, "ln") == 0)
        EXPR_UNARY(log);
    if (strcmp(name, "log") == 0 || strcmp(name, "log10") == 0)
        EXPR_UNARY(log10);
    if (strcmp(name, "floor") == 0)
        EXPR_UNARY(floor);
    if (strcmp(name, "ceil") == 0)
        EXPR_UNARY(ceil);
    if (strcmp(name, "round") == 0)
        EXPR_UNARY(round);
    if (strcmp(name, "deg") == 0) {
        if (argc != 1U)
            return false;
        *value = args[0] * 180.0 / M_PI;
        return true;
    }
    if (strcmp(name, "rad") == 0) {
        if (argc != 1U)
            return false;
        *value = args[0] * M_PI / 180.0;
        return true;
    }
    if (strcmp(name, "pow") == 0) {
        if (argc != 2U)
            return false;
        *value = pow(args[0], args[1]);
        return true;
    }
    if (strcmp(name, "min") == 0) {
        if (argc != 2U)
            return false;
        *value = fmin(args[0], args[1]);
        return true;
    }
    if (strcmp(name, "max") == 0) {
        if (argc != 2U)
            return false;
        *value = fmax(args[0], args[1]);
        return true;
    }
    if (strcmp(name, "atan2") == 0) {
        if (argc != 2U)
            return false;
        *value = atan2(args[0], args[1]);
        return true;
    }
#undef EXPR_UNARY
    return false;
}

static esp_err_t expr_eval_node(const solar_os_expr_program_t *program,
                                uint8_t index,
                                const solar_os_expr_context_t *context,
                                unsigned depth, double *value,
                                solar_os_expr_error_t *error)
{
    if (depth > EXPR_EVAL_DEPTH_MAX || index >= program->node_count) {
        expr_error_set(error, NULL, NULL, "evaluation depth exceeded");
        return ESP_ERR_INVALID_STATE;
    }
    const solar_os_expr_node_t *node = &program->nodes[index];
    switch (node->kind) {
    case SOLAR_OS_EXPR_NODE_NUMBER:
        *value = node->value.number;
        return ESP_OK;
    case SOLAR_OS_EXPR_NODE_VARIABLE:
        if (strcmp(node->value.name, "pi") == 0) {
            *value = M_PI;
            return ESP_OK;
        }
        if (strcmp(node->value.name, "e") == 0) {
            *value = M_E;
            return ESP_OK;
        }
        if (strcmp(node->value.name, "x") == 0 && context != NULL &&
            context->has_x) {
            *value = context->x;
            return ESP_OK;
        }
        if (context != NULL && context->variable != NULL &&
            context->variable(context->user, node->value.name, value)) {
            return ESP_OK;
        }
        expr_error_set(error, NULL, NULL, "unknown variable");
        return ESP_ERR_NOT_FOUND;
    case SOLAR_OS_EXPR_NODE_UNARY: {
        double child = 0.0;
        esp_err_t err = expr_eval_node(program, node->value.unary.child,
                                       context, depth + 1U, &child, error);
        if (err != ESP_OK)
            return err;
        *value = node->value.unary.op == '-' ? -child : child;
        return ESP_OK;
    }
    case SOLAR_OS_EXPR_NODE_BINARY: {
        double left = 0.0;
        double right = 0.0;
        esp_err_t err = expr_eval_node(program, node->value.binary.left,
                                       context, depth + 1U, &left, error);
        if (err != ESP_OK)
            return err;
        err = expr_eval_node(program, node->value.binary.right, context,
                             depth + 1U, &right, error);
        if (err != ESP_OK)
            return err;
        switch (node->value.binary.op) {
        case '+':
            *value = left + right;
            break;
        case '-':
            *value = left - right;
            break;
        case '*':
            *value = left * right;
            break;
        case '/':
            *value = left / right;
            break;
        case '%':
            *value = fmod(left, right);
            break;
        case '^':
            *value = pow(left, right);
            break;
        default:
            return ESP_ERR_INVALID_STATE;
        }
        return ESP_OK;
    }
    case SOLAR_OS_EXPR_NODE_CALL: {
        double args[SOLAR_OS_EXPR_ARG_MAX] = {0};
        for (size_t i = 0; i < node->value.call.argc; i++) {
            esp_err_t err =
                expr_eval_node(program, node->value.call.args[i], context,
                               depth + 1U, &args[i], error);
            if (err != ESP_OK)
                return err;
        }
        if (expr_builtin(node->value.call.name, args, node->value.call.argc,
                         value)) {
            return ESP_OK;
        }
        if (context != NULL && context->function != NULL &&
            context->function(context->user, node->value.call.name, args,
                              node->value.call.argc, value)) {
            return ESP_OK;
        }
        expr_error_set(error, NULL, NULL,
                       "unknown function or wrong argument count");
        return ESP_ERR_NOT_FOUND;
    }
    default:
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t solar_os_expr_evaluate(const solar_os_expr_program_t *program,
                                 const solar_os_expr_context_t *context,
                                 double *value, solar_os_expr_error_t *error)
{
    if (program == NULL || value == NULL || program->node_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
    }
    const esp_err_t err =
        expr_eval_node(program, program->root, context, 0U, value, error);
    if (err == ESP_OK && !isfinite(*value)) {
        expr_error_set(error, NULL, NULL, "result is outside the real domain");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}
