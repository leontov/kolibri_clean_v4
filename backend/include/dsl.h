#ifndef KOLIBRI_DSL_H
#define KOLIBRI_DSL_H

#include <stddef.h>

typedef enum {
    DSL_NODE_CONST,
    DSL_NODE_VAR_X,
    DSL_NODE_PARAM,
    DSL_NODE_ADD,
    DSL_NODE_SUB,
    DSL_NODE_MUL,
    DSL_NODE_DIV,
    DSL_NODE_SIN,
    DSL_NODE_EXP,
    DSL_NODE_LOG,
    DSL_NODE_POW,
    DSL_NODE_TANH
} DSLNodeType;

typedef struct DSLNode {
    DSLNodeType type;
    double value;
    int param_index;
    struct DSLNode *left;
    struct DSLNode *right;
} DSLNode;

DSLNode *dsl_node_const(double value);
DSLNode *dsl_node_var_x(void);
DSLNode *dsl_node_param(int param_index);
DSLNode *dsl_node_unary(DSLNodeType type, DSLNode *child);
DSLNode *dsl_node_binary(DSLNodeType type, DSLNode *left, DSLNode *right);
DSLNode *dsl_clone(const DSLNode *node);
void dsl_free(DSLNode *node);

double dsl_eval(const DSLNode *node, double x, const double *params, size_t param_count);
size_t dsl_complexity(const DSLNode *node);
void dsl_print_canonical(const DSLNode *node, char *buf, size_t buf_size);

/* Safe math helpers */
double safe_div(double a, double b);
double safe_log(double x);
double safe_pow(double base, double exp);
double safe_tanh(double x);

#endif /* KOLIBRI_DSL_H */
