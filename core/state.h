#ifndef KOLIBRI_STATE_H
#define KOLIBRI_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "metrics.h"

typedef struct KolFormula KolFormula;

typedef struct {
    const KolFormula *current;
    KolMetrics last;
    uint32_t step;
} KolState;

typedef struct {
    uint8_t digits[128];
    size_t length;
    uint8_t stride;
} KolEvent;

typedef struct {
    char formula[256];
    KolMetrics metrics;
    uint8_t leader;
    uint8_t digits[128];
    size_t digit_count;
    char text[128];
} KolOutput;

#endif
