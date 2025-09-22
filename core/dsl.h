#ifndef KOLIBRI_DSL_H
#define KOLIBRI_DSL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    KOL_NODE_CONST = 0,
    KOL_NODE_VAR_X,
    KOL_NODE_ADD,
    KOL_NODE_SUB,
    KOL_NODE_MUL,
    KOL_NODE_DIV,
    KOL_NODE_SIN,
    KOL_NODE_COS,
    KOL_NODE_TANH,
    KOL_NODE_EXP,
    KOL_NODE_LOG,
    KOL_NODE_ABS,
    KOL_NODE_CLAMP,
    KOL_NODE_IFZ
} KolNodeType;

typedef struct KolFormula {
    KolNodeType type;
    double value;
    struct KolFormula *a;
    struct KolFormula *b;
    struct KolFormula *c;
} KolFormula;

KolFormula *dsl_const(double value);
KolFormula *dsl_var(void);
KolFormula *dsl_node(KolNodeType type, KolFormula *a, KolFormula *b, KolFormula *c);
KolFormula *dsl_clone(const KolFormula *src);
KolFormula *dsl_rand(uint32_t *rng, int max_depth);
KolFormula *dsl_mutate(const KolFormula *src, uint32_t *rng);
KolFormula *dsl_simplify(const KolFormula *src);
double      dsl_eval(const KolFormula *f, double x);
int         dsl_complexity(const KolFormula *f);
void        dsl_free(KolFormula *f);
char       *dsl_print(const KolFormula *f);

#endif
