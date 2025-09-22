#ifndef KOLIBRI_ENGINE_H
#define KOLIBRI_ENGINE_H

#include <stddef.h>
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
    uint8_t last_digits[128];
    size_t last_digit_count;
    char last_text[128];
} KolEngine;

KolEngine *engine_create(uint8_t depth, uint32_t seed);
void       engine_free(KolEngine *engine);
int        engine_tick(KolEngine *engine, const KolEvent *in, KolOutput *out);
int        engine_ingest_text(KolEngine *engine, const char *utf8, KolEvent *out_event);
int        engine_ingest_digits(KolEngine *engine, const uint8_t *digits, size_t len, KolEvent *out_event);
int        engine_ingest_bytes(KolEngine *engine, const uint8_t *bytes, size_t len, KolEvent *out_event);
int        engine_ingest_signal(KolEngine *engine, const float *samples, size_t len, KolEvent *out_event);
int        engine_render_digits(KolEngine *engine, uint8_t *digits, size_t max_len, size_t *out_len);
int        engine_render_text(KolEngine *engine, char *buf, size_t cap);

#endif
