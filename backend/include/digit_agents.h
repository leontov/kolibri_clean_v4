#ifndef KOLIBRI_DIGIT_AGENTS_H
#define KOLIBRI_DIGIT_AGENTS_H

#include "common.h"

typedef struct DigitAgent {
    double weight;
    uint64_t seed;
    struct DigitAgent* sub[10];
} DigitAgent;

typedef struct {
    DigitAgent* root[10];
    int depth_max;
} DigitField;

typedef struct {
    double vote[10];
    double temperature;
} VoteState;

bool digit_field_init(DigitField* f, int depth_max, uint64_t seed);
void digit_field_free(DigitField* f);
void digit_tick(DigitField* f, const double external[10]);
void digit_aggregate(const DigitField* f, VoteState* out);

#endif
