#ifndef KOLIBRI_DIGIT_H
#define KOLIBRI_DIGIT_H

#include <stddef.h>
#include <stdint.h>

#include "metrics.h"
#include "rng.h"
#include "state.h"

typedef struct KolFormula KolFormula;
typedef struct KolDigit KolDigit;

typedef struct {
    KolFormula *formula;
    KolMetrics metrics;
} KolExperience;

struct KolDigit {
    uint8_t id;
    uint8_t depth;
    KolRng rng;
    KolDigit *children[10];
    KolExperience memory[8];
    size_t memory_count;
};

KolDigit *digit_create(uint8_t id, uint8_t depth, uint32_t seed);
void      digit_free(KolDigit *digit);
float     digit_vote(KolDigit *digit, const KolState *state);
void      digit_learn(KolDigit *digit, const KolFormula *formula, const KolMetrics *metrics);

#endif
