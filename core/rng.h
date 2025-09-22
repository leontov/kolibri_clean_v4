#ifndef KOLIBRI_RNG_H
#define KOLIBRI_RNG_H

#include <stdint.h>

typedef struct {
    uint32_t state;
} KolRng;

void     rng_seed(KolRng *rng, uint32_t seed);
uint32_t rng_next(KolRng *rng);
double   rng_uniform(KolRng *rng, double min, double max);
double   rng_normalized(KolRng *rng);

#endif
