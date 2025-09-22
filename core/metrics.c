#include "metrics.h"

#include <math.h>
#include <stddef.h>

#include "dsl.h"

static double clamp01(double v) {
    if (v < 0.0) {
        return 0.0;
    }
    if (v > 1.0) {
        return 1.0;
    }
    return v;
}

KolMetrics metrics_eval(const KolFormula *formula, const KolDataset *dataset) {
    KolMetrics m = {0.0, 0.0, 0.0};
    if (!formula || !dataset || dataset->count == 0 || !dataset->xs || !dataset->ys) {
        return m;
    }
    double err = 0.0;
    double prev = 0.0;
    double diff_sum = 0.0;
    for (size_t i = 0; i < dataset->count; ++i) {
        double x = dataset->xs[i];
        double y = dataset->ys[i];
        double pred = dsl_eval(formula, x);
        double delta = pred - y;
        err += delta * delta;
        if (i > 0) {
            double d = pred - prev;
            diff_sum += d * d;
        }
        prev = pred;
    }
    double rmse = sqrt(err / (double)dataset->count);
    double max_ref = 1.0 + fabs(dataset->ys[0]);
    m.eff = clamp01(1.0 / (1.0 + rmse / max_ref));
    int compl = dsl_complexity(formula);
    m.compl = (double)compl;
    double var = diff_sum / (double)(dataset->count ? dataset->count : 1);
    m.stab = clamp01(1.0 / (1.0 + var));
    return m;
}
