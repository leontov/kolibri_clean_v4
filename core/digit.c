#include "digit.h"

#include <stdlib.h>
#include <string.h>

#include "dsl.h"

static KolExperience make_experience(const KolFormula *formula, const KolMetrics *metrics) {
    KolExperience e;
    e.formula = formula ? dsl_clone(formula) : NULL;
    e.metrics = metrics ? *metrics : (KolMetrics){0.0, 0.0, 0.0};
    return e;
}

KolDigit *digit_create(uint8_t id, uint8_t depth, uint32_t seed) {
    KolDigit *digit = (KolDigit *)calloc(1, sizeof(KolDigit));
    if (!digit) {
        return NULL;
    }
    digit->id = id;
    digit->depth = depth;
    rng_seed(&digit->rng, seed + id * 97u + depth * 13u);
    for (int i = 0; i < 10; ++i) {
        digit->children[i] = NULL;
    }
    digit->memory_count = 0;
    return digit;
}

static void free_experience(KolExperience *exp) {
    if (!exp) {
        return;
    }
    dsl_free(exp->formula);
    exp->formula = NULL;
}

void digit_free(KolDigit *digit) {
    if (!digit) {
        return;
    }
    for (size_t i = 0; i < digit->memory_count; ++i) {
        free_experience(&digit->memory[i]);
    }
    for (int i = 0; i < 10; ++i) {
        if (digit->children[i]) {
            digit_free(digit->children[i]);
        }
    }
    free(digit);
}

float digit_vote(KolDigit *digit, const KolState *state) {
    if (!digit || !state) {
        return 0.0f;
    }
    double base = rng_normalized(&digit->rng);
    double memory_bonus = 0.0;
    if (digit->memory_count > 0) {
        memory_bonus = digit->memory[digit->memory_count - 1].metrics.eff;
    }
    double step_factor = (state->step % 17u == digit->id) ? 0.2 : 0.0;
    return (float)(base * 0.6 + memory_bonus * 0.3 + state->last.eff * 0.1 + step_factor);
}

void digit_learn(KolDigit *digit, const KolFormula *formula, const KolMetrics *metrics) {
    if (!digit) {
        return;
    }
    KolExperience exp = make_experience(formula, metrics);
    if (digit->memory_count < sizeof(digit->memory) / sizeof(digit->memory[0])) {
        digit->memory[digit->memory_count++] = exp;
    } else {
        size_t idx = (size_t)(rng_next(&digit->rng) % digit->memory_count);
        free_experience(&digit->memory[idx]);
        digit->memory[idx] = exp;
    }
}
