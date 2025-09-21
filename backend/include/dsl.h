#ifndef KOLIBRI_DSL_H
#define KOLIBRI_DSL_H
#include "common.h"

typedef enum { F_CONST, F_VAR_X, F_ADD, F_SUB, F_MUL, F_DIV, F_SIN } NodeType;

typedef struct Formula {
    NodeType t;
    double v;
    struct Formula* a;
    struct Formula* b;
} Formula;

Formula* f_const(double v);
Formula* f_x();
Formula* f_add(Formula* a, Formula* b);
Formula* f_sub(Formula* a, Formula* b);
Formula* f_mul(Formula* a, Formula* b);
Formula* f_div(Formula* a, Formula* b);
Formula* f_sin(Formula* a);

double f_eval(const Formula* f, double x);
int    f_complexity(const Formula* f);
int    f_render(const Formula* f, char* out, size_t n);
void   f_free(Formula* f);

#endif
