#include "fractal.h"

#include <stdlib.h>

static KolDigit *create_digit(uint8_t id, uint8_t depth, uint32_t seed) {
    KolDigit *digit = digit_create(id, depth, seed);
    if (!digit) {
        return NULL;
    }
    if (depth == 0) {
        return digit;
    }
    for (uint8_t i = 0; i < 10; ++i) {
        digit->children[i] = create_digit(i, (uint8_t)(depth - 1), seed + (uint32_t)i * 31u + (uint32_t)id * 17u);
    }
    return digit;
}

KolFractal *fractal_create(uint8_t depth, uint32_t seed) {
    KolFractal *fractal = (KolFractal *)calloc(1, sizeof(KolFractal));
    if (!fractal) {
        return NULL;
    }
    fractal->seed = seed;
    fractal->depth = depth;
    fractal->root = create_digit(0, depth, seed);
    if (!fractal->root) {
        free(fractal);
        return NULL;
    }
    return fractal;
}

void fractal_free(KolFractal *fractal) {
    if (!fractal) {
        return;
    }
    digit_free(fractal->root);
    free(fractal);
}

KolDigit *fractal_root(KolFractal *fractal) {
    if (!fractal) {
        return NULL;
    }
    return fractal->root;
}
