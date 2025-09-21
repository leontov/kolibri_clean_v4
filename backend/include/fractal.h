#ifndef KOLIBRI_FRACTAL_H
#define KOLIBRI_FRACTAL_H

#include "dsl.h"
#include <stddef.h>
#include <stdint.h>

#define KOLIBRI_FA_DIGITS 10
#define KOLIBRI_FA_LEVELS 10
#define KOLIBRI_FA_MAX_OPS 6

typedef enum {
    FRACTAL_OP_NONE = 0,
    FRACTAL_OP_ADD_CONST,
    FRACTAL_OP_MUL_CONST,
    FRACTAL_OP_ADD_PARAM,
    FRACTAL_OP_SUB_PARAM,
    FRACTAL_OP_APPLY_SIN,
    FRACTAL_OP_APPLY_TANH,
    FRACTAL_OP_APPLY_EXP,
    FRACTAL_OP_APPLY_LOG,
    FRACTAL_OP_POW_PARAM
} FractalOpType;

typedef struct {
    FractalOpType type;
    double value;
    int param_index;
} FractalOp;

typedef struct {
    size_t op_count;
    FractalOp ops[KOLIBRI_FA_MAX_OPS];
} FractalTransform;

typedef struct {
    double r;
    size_t level_count;
    size_t stab_window;
    FractalTransform transforms[KOLIBRI_FA_LEVELS][KOLIBRI_FA_DIGITS];
} FractalMap;

int fractal_load_map(const char *path, FractalMap *map, char *err, size_t err_size);

void fractal_generate_votes(uint64_t seed, double votes[KOLIBRI_FA_DIGITS]);
void fractal_votes_to_address(const double votes[KOLIBRI_FA_DIGITS], char fa_out[KOLIBRI_FA_DIGITS + 1]);
size_t fractal_stability(const char window[][KOLIBRI_FA_DIGITS + 1], size_t count);

DSLNode *fractal_build_formula(const FractalMap *map, const char *fa, const double votes[KOLIBRI_FA_DIGITS]);

#endif /* KOLIBRI_FRACTAL_H */
