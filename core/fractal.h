#ifndef KOLIBRI_FRACTAL_H
#define KOLIBRI_FRACTAL_H

#include <stdint.h>

#include "digit.h"

typedef struct {
    KolDigit *root;
    uint8_t depth;
    uint32_t seed;
} KolFractal;

KolFractal *fractal_create(uint8_t depth, uint32_t seed);
void        fractal_free(KolFractal *fractal);
KolDigit   *fractal_root(KolFractal *fractal);

#endif
