#include "rng.h"

#include <math.h>

#define KOLIBRI_RNG_INIT 0x12345678u

void rng_seed(KolRng *rng, uint32_t seed) {
    if (!seed) {
        seed = KOLIBRI_RNG_INIT;
    }
    rng->state = seed;
}

uint32_t rng_next(KolRng *rng) {
    uint32_t x = rng->state;
    if (!x) {
        x = KOLIBRI_RNG_INIT;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

double rng_normalized(KolRng *rng) {
    return (double)rng_next(rng) / (double)UINT32_MAX;
}

double rng_uniform(KolRng *rng, double min, double max) {
    double u = rng_normalized(rng);
    return min + (max - min) * u;
}
