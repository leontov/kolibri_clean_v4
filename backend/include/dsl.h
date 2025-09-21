#ifndef KOLIBRI_DSL_H
#define KOLIBRI_DSL_H

#include <stddef.h>

typedef enum {
    DSL_NODE_CONST,
    DSL_NODE_VAR_X,
    DSL_NODE_ADD,
    DSL_NODE_SUB,
    DSL_NODE_MUL,
    DSL_NODE_DIV,
    DSL_NODE_SIN,
    DSL_NODE_EXP,
    DSL_NODE_LOG,
    DSL_NODE_POW,
    DSL_NODE_TANH,
    DSL_NODE_PARAM
} dsl_node_type_t;

typedef struct {
    double params[6];
    char repr[512];
    size_t node_count;
} kolibri_formula_t;

void dsl_build_formula(const double votes[10], kolibri_formula_t *out);
double dsl_evaluate(const kolibri_formula_t *formula, double x);

double dsl_safe_log(double x);
double dsl_safe_div(double numerator, double denominator);
double dsl_safe_pow(double base, double exponent);

double dsl_compute_eff(double mse);

double dsl_compute_complexity_score(size_t node_count);

#endif
