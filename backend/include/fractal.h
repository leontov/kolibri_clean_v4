#ifndef KOLIBRI_FRACTAL_H
#define KOLIBRI_FRACTAL_H
#include "dsl.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char id[32];
    double r;
    double coeff_sin_a;
    double coeff_sin_omega;
    double coeff_linear;
    double coeff_quadratic;
    double coeff_tanh;
    double coeff_exp_amp;
    double coeff_exp_gamma;
    double coeff_log_eps;
    double coeff_mix_sin;
    double coeff_mix_cos;
    double coeff_mix_phi;
    double coeff_pow_amp;
    double coeff_pow_exp;
    double coeff_reduce;
} FractalMap;

bool fractal_map_load(const char* path, FractalMap* map);
void fractal_address_from_votes(const double votes[10], char out[11]);
int fractal_common_prefix_len(const char addrs[][11], size_t n);
Formula* fractal_build_formula(const char* fa, const FractalMap* map);

#endif
