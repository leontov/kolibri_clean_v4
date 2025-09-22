#ifndef KOLIBRI_ENGINE_H
#define KOLIBRI_ENGINE_H

#include <stdint.h>

#include "chain.h"
#include "dsl.h"
#include "fractal.h"
#include "metrics.h"
#include "state.h"
#include "vote.h"

typedef struct {
    KolFractal *fractal;
    KolFormula *current;
    KolMetrics last;
    uint32_t step;
    KolDataset dataset;
    double xs[32];
    double ys[32];
} KolEngine;

KolEngine *engine_create(uint8_t depth, uint32_t seed);
void       engine_free(KolEngine *engine);
int        engine_tick(KolEngine *engine, const KolEvent *in, KolOutput *out);
int        engine_ingest_text(KolEngine *engine, const char *utf8, KolEvent *out_event);

#endif
