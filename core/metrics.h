#ifndef KOLIBRI_METRICS_H
#define KOLIBRI_METRICS_H

#include <stddef.h>

typedef struct {
    const double *xs;
    const double *ys;
    size_t count;
} KolDataset;

typedef struct {
    double eff;
    double compl;
    double stab;
} KolMetrics;

struct KolFormula;

KolMetrics metrics_eval(const struct KolFormula *formula, const KolDataset *dataset);

#endif
