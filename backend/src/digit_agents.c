#include "vote_aggregate.h"
#include <math.h>
#include <string.h>

static double clamp01(double x){
    if(x < 0.0) return 0.0;
    if(x > 1.0) return 1.0;
    return x;
}

void digit_aggregate(double out[10], const VotePolicy* policy,
                     const double layers[][10], size_t layer_count){
    if(!out) return;
    memset(out, 0, sizeof(double) * 10);
    if(layer_count == 0 || !layers) return;

    double decay = 1.0;
    if(policy){
        decay = policy->depth_decay;
    }
    if(decay < 0.0) decay = 0.0;
    if(decay > 1.0) decay = 1.0;

    double total_weight = 0.0;
    for(size_t depth = 0; depth < layer_count; ++depth){
        double weight;
        if(depth == 0){
            weight = 1.0;
        }else if(decay == 0.0){
            weight = 0.0;
        }else{
            weight = pow(decay, (double)depth);
        }
        if(weight == 0.0) continue;
        for(int digit = 0; digit < 10; ++digit){
            out[digit] += layers[depth][digit] * weight;
        }
        total_weight += weight;
    }

    if(total_weight > 0.0){
        for(int digit = 0; digit < 10; ++digit){
            out[digit] = clamp01(out[digit] / total_weight);
        }
    }
}
