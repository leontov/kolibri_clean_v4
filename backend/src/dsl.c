#include "dsl.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DSLNode *dsl_alloc(DSLNodeType type) {
    DSLNode *node = (DSLNode *)calloc(1, sizeof(DSLNode));
    if (node != NULL) {
        node->type = type;
    }
    return node;
}

DSLNode *dsl_node_const(double value) {
    DSLNode *node = dsl_alloc(DSL_NODE_CONST);
    if (node != NULL) {
        node->value = value;
    }
    return node;
}

DSLNode *dsl_node_var_x(void) {
    return dsl_alloc(DSL_NODE_VAR_X);
}

DSLNode *dsl_node_param(int param_index) {
    DSLNode *node = dsl_alloc(DSL_NODE_PARAM);
    if (node != NULL) {
        node->param_index = param_index;
    }
    return node;
}

DSLNode *dsl_node_unary(DSLNodeType type, DSLNode *child) {
    DSLNode *node = dsl_alloc(type);
    if (node != NULL) {
        node->left = child;
    }
    return node;
}

DSLNode *dsl_node_binary(DSLNodeType type, DSLNode *left, DSLNode *right) {
    DSLNode *node = dsl_alloc(type);
    if (node != NULL) {
        node->left = left;
        node->right = right;
    }
    return node;
}

static double get_param(const double *params, size_t param_count, int index) {
    if (params == NULL || index < 0 || (size_t)index >= param_count) {
        return 0.0;
    }
    return params[index];
}

double safe_div(double a, double b) {
    if (b == 0.0) {
        b = (a >= 0.0) ? 1e-9 : -1e-9;
    }
    if (b == 0.0) {
        b = 1e-9;
    }
    return a / b;
}

double safe_log(double x) {
    if (x <= 1e-12) {
        x = 1e-12;
    }
    return log(x);
}

double safe_pow(double base, double exp) {
    if (base < 0.0) {
        double rounded = floor(exp + 0.5);
        if (fabs(exp - rounded) < 1e-9) {
            double val = pow(fabs(base), rounded);
            if (fmod(rounded, 2.0) != 0.0) {
                val = -val;
            }
            return val;
        }
        base = fabs(base);
    }
    return pow(base, exp);
}

double safe_tanh(double x) {
    if (x > 20.0) {
        return 1.0;
    }
    if (x < -20.0) {
        return -1.0;
    }
    return tanh(x);
}

double dsl_eval(const DSLNode *node, double x, const double *params, size_t param_count) {
    if (node == NULL) {
        return 0.0;
    }
    switch (node->type) {
        case DSL_NODE_CONST:
            return node->value;
        case DSL_NODE_VAR_X:
            return x;
        case DSL_NODE_PARAM:
            return get_param(params, param_count, node->param_index);
        case DSL_NODE_ADD:
            return dsl_eval(node->left, x, params, param_count) +
                   dsl_eval(node->right, x, params, param_count);
        case DSL_NODE_SUB:
            return dsl_eval(node->left, x, params, param_count) -
                   dsl_eval(node->right, x, params, param_count);
        case DSL_NODE_MUL:
            return dsl_eval(node->left, x, params, param_count) *
                   dsl_eval(node->right, x, params, param_count);
        case DSL_NODE_DIV:
            return safe_div(dsl_eval(node->left, x, params, param_count),
                            dsl_eval(node->right, x, params, param_count));
        case DSL_NODE_SIN:
            return sin(dsl_eval(node->left, x, params, param_count));
        case DSL_NODE_EXP: {
            double v = dsl_eval(node->left, x, params, param_count);
            if (v > 20.0) {
                v = 20.0;
            }
            if (v < -20.0) {
                v = -20.0;
            }
            return exp(v);
        }
        case DSL_NODE_LOG:
            return safe_log(dsl_eval(node->left, x, params, param_count));
        case DSL_NODE_POW:
            return safe_pow(dsl_eval(node->left, x, params, param_count),
                            dsl_eval(node->right, x, params, param_count));
        case DSL_NODE_TANH:
            return safe_tanh(dsl_eval(node->left, x, params, param_count));
    }
    return 0.0;
}

size_t dsl_complexity(const DSLNode *node) {
    if (node == NULL) {
        return 0;
    }
    size_t left = dsl_complexity(node->left);
    size_t right = dsl_complexity(node->right);
    return 1 + left + right;
}


DSLNode *dsl_clone(const DSLNode *node) {
    if (node == NULL) {
        return NULL;
    }
    DSLNode *copy = dsl_alloc(node->type);
    if (copy == NULL) {
        return NULL;
    }
    copy->value = node->value;
    copy->param_index = node->param_index;
    copy->left = dsl_clone(node->left);
    copy->right = dsl_clone(node->right);
    return copy;
}

void dsl_free(DSLNode *node) {
    if (node == NULL) {
        return;
    }
    dsl_free(node->left);
    dsl_free(node->right);
    free(node);
}

static void append_str(char **cursor, size_t *remaining, const char *str) {
    if (*remaining == 0) {
        return;
    }
    size_t len = strlen(str);
    if (len >= *remaining) {
        len = *remaining - 1;
    }
    memcpy(*cursor, str, len);
    *cursor += len;
    **cursor = '\0';
    *remaining -= len;
}

static void append_fmt(char **cursor, size_t *remaining, const char *fmt, ...) {
    if (*remaining == 0) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(*cursor, *remaining, fmt, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    if ((size_t)written >= *remaining) {
        written = (int)(*remaining - 1);
    }
    *cursor += written;
    *remaining -= (size_t)written;
}

static void append_double(char **cursor, size_t *remaining, double value) {
    if (*remaining == 0) {
        return;
    }
    if (value == 0.0) {
        value = 0.0;
    }
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%.17g", value);
    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(buf)) {
        len = (int)sizeof(buf) - 1;
    }
    buf[len] = '\0';
    append_str(cursor, remaining, buf);
}

static void print_node(const DSLNode *node, char **cursor, size_t *remaining) {
    if (node == NULL) {
        append_str(cursor, remaining, "0");
        return;
    }
    switch (node->type) {
        case DSL_NODE_CONST:
            append_double(cursor, remaining, node->value);
            break;
        case DSL_NODE_VAR_X:
            append_str(cursor, remaining, "x");
            break;
        case DSL_NODE_PARAM:
            append_fmt(cursor, remaining, "c%d", node->param_index);
            break;
        case DSL_NODE_ADD:
            append_str(cursor, remaining, "add(");
            print_node(node->left, cursor, remaining);
            append_str(cursor, remaining, ",");
            print_node(node->right, cursor, remaining);
            append_str(cursor, remaining, ")");
            break;
        case DSL_NODE_SUB:
            append_str(cursor, remaining, "sub(");
            print_node(node->left, cursor, remaining);
            append_str(cursor, remaining, ",");
            print_node(node->right, cursor, remaining);
            append_str(cursor, remaining, ")");
            break;
        case DSL_NODE_MUL:
            append_str(cursor, remaining, "mul(");
            print_node(node->left, cursor, remaining);
            append_str(cursor, remaining, ",");
            print_node(node->right, cursor, remaining);
            append_str(cursor, remaining, ")");
            break;
        case DSL_NODE_DIV:
            append_str(cursor, remaining, "div(");
            print_node(node->left, cursor, remaining);
            append_str(cursor, remaining, ",");
            print_node(node->right, cursor, remaining);
            append_str(cursor, remaining, ")");
            break;
        case DSL_NODE_SIN:
            append_str(cursor, remaining, "sin(");
            print_node(node->left, cursor, remaining);
            append_str(cursor, remaining, ")");
            break;
        case DSL_NODE_EXP:
            append_str(cursor, remaining, "exp(");
            print_node(node->left, cursor, remaining);
            append_str(cursor, remaining, ")");
            break;
        case DSL_NODE_LOG:
            append_str(cursor, remaining, "log(");
            print_node(node->left, cursor, remaining);
            append_str(cursor, remaining, ")");
            break;
        case DSL_NODE_POW:
            append_str(cursor, remaining, "pow(");
            print_node(node->left, cursor, remaining);
            append_str(cursor, remaining, ",");
            print_node(node->right, cursor, remaining);
            append_str(cursor, remaining, ")");
            break;
        case DSL_NODE_TANH:
            append_str(cursor, remaining, "tanh(");
            print_node(node->left, cursor, remaining);
            append_str(cursor, remaining, ")");
            break;
    }
}

void dsl_print_canonical(const DSLNode *node, char *buf, size_t buf_size) {
    if (buf_size == 0) {
        return;
    }
    buf[0] = '\0';
    char *cursor = buf;
    size_t remaining = buf_size;
    print_node(node, &cursor, &remaining);
    if (remaining > 0) {
        *cursor = '\0';
    } else {
        buf[buf_size - 1] = '\0';
    }
}
