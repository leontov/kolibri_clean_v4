#ifndef KOLIBRI_DIGIT_AGENTS_H
#define KOLIBRI_DIGIT_AGENTS_H

#include "common.h"
#include <stdbool.h>

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

bool digit_field_init(DigitField* field, int depth_max, uint64_t seed);
void digit_field_free(DigitField* field);
void digit_tick(DigitField* field);
void digit_aggregate(const DigitField* field, VoteState* out);

#endif
