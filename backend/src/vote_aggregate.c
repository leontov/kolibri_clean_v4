#include "vote_aggregate.h"
#include <math.h>
#include <string.h>

static double clamp01(double v) {
    if (v < 0.0) {
        return 0.0;
    }
    if (v > 1.0) {
        return 1.0;
    }
    return v;
}

void vote_apply_policy(VoteState* state, const VotePolicy* policy) {
    if (!state || !policy) {
        return;
    }

    double decay = policy->depth_decay;
    if (decay < 0.0) {
        decay = 0.0;
    } else if (decay > 1.0) {
        decay = 1.0;
    }
    double quorum = policy->quorum;
    if (quorum < 0.0) {
        quorum = 0.0;
    } else if (quorum > 1.0) {
        quorum = 1.0;
    }

    const double uniform = 1.0 / 10.0;
    double adjusted[10];
    for (int i = 0; i < 10; ++i) {
        double v = clamp01(state->vote[i]);
        v = decay * v + (1.0 - decay) * uniform;
        if (v < quorum) {
            v = 0.0;
        }
        adjusted[i] = v;
    }

    double temperature = state->temperature;
    if (temperature <= 0.0) {
        temperature = 1e-3;
    }

    double max_v = adjusted[0];
    for (int i = 1; i < 10; ++i) {
        if (adjusted[i] > max_v) {
            max_v = adjusted[i];
        }
    }

    double sum = 0.0;
    for (int i = 0; i < 10; ++i) {
        double scaled = (adjusted[i] - max_v) / temperature;
        double e = exp(scaled);
        adjusted[i] = e;
        sum += e;
    }

    if (sum <= 0.0) {
        for (int i = 0; i < 10; ++i) {
            state->vote[i] = uniform;
        }
        return;
    }

    for (int i = 0; i < 10; ++i) {
        state->vote[i] = adjusted[i] / sum;
    }
}
