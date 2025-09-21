#ifndef KOLIBRI_DSL_H
#define KOLIBRI_DSL_H
#include "common.h"

typedef enum {
    F_CONST,
    F_PARAM,
    F_VAR_X,
    F_ADD,
    F_SUB,
    F_MUL,
    F_DIV,
    F_MIN,
    F_MAX,
    F_SIN,
    F_COS,
    F_EXP,
    F_LOG,
    F_POW,
    F_TANH,
    F_SIGMOID,
    F_ABS
} NodeType;

typedef struct Formula {
    NodeType t;
    double v;
    int param_index;
    struct Formula* a;
    struct Formula* b;
} Formula;

Formula* f_const(double v);
Formula* f_param(int idx);
Formula* f_x();
Formula* f_add(Formula* a, Formula* b);
Formula* f_sub(Formula* a, Formula* b);
Formula* f_mul(Formula* a, Formula* b);
Formula* f_div(Formula* a, Formula* b);
Formula* f_min(Formula* a, Formula* b);
Formula* f_max(Formula* a, Formula* b);
Formula* f_sin(Formula* a);
Formula* f_cos(Formula* a);
Formula* f_exp(Formula* a);
Formula* f_log(Formula* a);
Formula* f_pow(Formula* a, Formula* b);
Formula* f_tanh(Formula* a);
Formula* f_sigmoid(Formula* a);
Formula* f_abs(Formula* a);

double f_eval(const Formula* f, const double* params, size_t param_count, double x);
double f_eval_grad(const Formula* f, const double* params, size_t param_count,
                   double x, double* grad_out);
int    f_complexity(const Formula* f);
int    f_render(const Formula* f, char* out, size_t n);
int    f_max_param_index(const Formula* f);
void   f_free(Formula* f);

#endif
