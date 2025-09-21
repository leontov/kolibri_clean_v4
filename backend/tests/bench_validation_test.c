#include "reason.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int nearly_equal(double a, double b){
    double diff = fabs(a - b);
    double scale = fabs(a) + fabs(b);
    if(scale < 1.0){
        scale = 1.0;
    }
    return diff <= (1e-12 * scale);
}

int main(void){
    ReasonBlock block = {0};
    const double baseline[10] = {
        0.2, 0.5, 0.7, 1.0, 0.3, 0.9, 0.8, 0.4, 0.6, 0.55
    };

    for(size_t i = 0; i < 10; ++i){
        block.bench_eff[i] = baseline[i];
    }

    double score = rb_bench_validation_score(&block, 0.5);
    if(!nearly_equal(score, 5.05 / 7.0)){
        fprintf(stderr, "unexpected score %.15f\n", score);
        return EXIT_FAILURE;
    }

    double high_threshold = rb_bench_validation_score(&block, 0.95);
    if(!nearly_equal(high_threshold, 1.0)){
        fprintf(stderr, "unexpected high-threshold score %.15f\n", high_threshold);
        return EXIT_FAILURE;
    }

    block = (ReasonBlock){0};
    for(size_t i = 0; i < 10; ++i){
        block.bench_eff[i] = baseline[i];
    }
    block.bench_eff[3] = NAN;
    block.bench_eff[5] = INFINITY;
    block.bench_eff[6] = -INFINITY;

    double sanitized = rb_bench_validation_score(&block, NAN);
    if(!nearly_equal(sanitized, 3.25 / 7.0)){
        fprintf(stderr, "unexpected sanitized score %.15f\n", sanitized);
        return EXIT_FAILURE;
    }

    double unreachable = rb_bench_validation_score(&block, 2.0);
    if(!nearly_equal(unreachable, 0.0)){
        fprintf(stderr, "unexpected unreachable score %.15f\n", unreachable);
        return EXIT_FAILURE;
    }

    double null_score = rb_bench_validation_score(NULL, 0.5);
    if(null_score != 0.0){
        fprintf(stderr, "null score should be zero but was %.15f\n", null_score);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
