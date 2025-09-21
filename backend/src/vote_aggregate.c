#include "vote_aggregate.h"

#include <stddef.h>

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

    double decay = clamp01(policy->depth_decay);
    double quorum = clamp01(policy->quorum);
    double smoothing = clamp01(state->temperature);

    for (size_t i = 0; i < 10; ++i) {
        double v = clamp01(state->vote[i]);

        if (decay > 0.0) {
            v = decay * v + (1.0 - decay) * 0.5;
        }

        if (v < quorum) {
            state->vote[i] = 0.0;
            continue;
        }

        if (smoothing > 0.0) {
            double span = 1.0 - quorum;
            double normalized = span > 0.0 ? (v - quorum) / span : 0.0;
            normalized = normalized * (1.0 - smoothing) + 0.5 * smoothing;
            v = quorum + normalized * span;
        }

        state->vote[i] = clamp01(v);
    }
}

