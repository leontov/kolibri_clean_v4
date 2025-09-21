#include "dsl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static double clamp(double v, double min, double max) {
    if (v < min) {
        return min;
    }
    if (v > max) {
        return max;
    }
    return v;
}

double dsl_safe_log(double x) {
    const double eps = 1e-9;
    if (x < eps) {
        x = eps;
    }
    return log(x);
}

double dsl_safe_div(double numerator, double denominator) {
    const double eps = 1e-9;
    if (fabs(denominator) < eps) {
        denominator = (denominator >= 0.0) ? eps : -eps;
    }
    return numerator / denominator;
}

double dsl_safe_pow(double base, double exponent) {
    if (base < 0.0 && floor(exponent) != exponent) {
        base = fabs(base);
    }
    return pow(base, exponent);
}

double dsl_compute_eff(double mse) {
    if (mse < 0.0) {
        mse = 0.0;
    }
    return 1.0 / (1.0 + mse);
}

double dsl_compute_complexity_score(size_t node_count) {
    return 1.0 + (double)node_count;
}

void dsl_build_formula(const double votes[10], kolibri_formula_t *out) {
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    for (size_t i = 0; i < 6; ++i) {
        double v = votes[i];
        if (v < 0.0) {
            v = 0.0;
        }
        if (v > 1.0) {
            v = 1.0;
        }
        out->params[i] = (v * 2.0) - 1.0;
    }
    snprintf(out->repr, sizeof(out->repr),
             "ADD(ADD(PARAM(c0), MUL(PARAM(c1), VAR_X)), ADD(MUL(PARAM(c2), POW(VAR_X, CONST(2)))), ADD(TANH(MUL(PARAM(c3), VAR_X)), MUL(SIN(MUL(PARAM(c4), VAR_X)), EXP(MUL(CONST(-0.1), PARAM(c5))))))");
    out->node_count = 19; /* derived from the primitives above */
}

double dsl_evaluate(const kolibri_formula_t *formula, double x) {
    if (!formula) {
        return 0.0;
    }
    double p0 = formula->params[0];
    double p1 = formula->params[1];
    double p2 = formula->params[2];
    double p3 = formula->params[3];
    double p4 = formula->params[4];
    double p5 = formula->params[5];

    double quad = p0 + p1 * x + p2 * x * x;
    double non_linear = tanh(p3 * x);
    double sinus = sin(p4 * x);
    double exp_term = exp(-0.1 * fabs(p5));
    return quad + non_linear + sinus * exp_term;
}
