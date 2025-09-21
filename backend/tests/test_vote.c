#include "vote_aggregate.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void assert_close(double a, double b){
    double diff = fabs(a - b);
    if(diff > 1e-6){
        fprintf(stderr, "Expected %.6f got %.6f (diff %.6f)\n", b, a, diff);
        assert(diff <= 1e-6);
    }
}

int main(void){
    double layers[3][10] = {{0}};
    layers[0][0] = 1.0;
    layers[1][1] = 1.0;
    layers[2][0] = 1.0;
    layers[2][1] = 1.0;

    VotePolicy policy = {1.0, 0.0, 0.0};
    double out[10];

    digit_aggregate(out, &policy, (const double (*)[10])layers, 3);
    assert_close(out[0], 2.0/3.0);
    assert_close(out[1], 2.0/3.0);

    policy.depth_decay = 0.0;
    digit_aggregate(out, &policy, (const double (*)[10])layers, 3);
    assert_close(out[0], 1.0);
    assert_close(out[1], 0.0);

    policy.depth_decay = 0.5;
    digit_aggregate(out, &policy, (const double (*)[10])layers, 3);
    assert_close(out[0], 1.25/1.75);
    assert_close(out[1], 0.75/1.75);

    return 0;
}
