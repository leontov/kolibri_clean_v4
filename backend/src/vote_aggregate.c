#include "vote_aggregate.h"

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

    double prior_mix = clamp01(policy->depth_decay);
    double quorum = clamp01(policy->quorum);
    double soft = clamp01(policy->temperature);

    for (int i = 0; i < 10; ++i) {
        double v = clamp01(state->vote[i]);

        if (prior_mix > 0.0) {
            v = prior_mix * v + (1.0 - prior_mix) * 0.5;
        }

        if (v < quorum) {
            state->vote[i] = 0.0;
            continue;
        }

        if (soft > 0.0) {
            double span = 1.0 - quorum;
            double normalized = span > 0.0 ? (v - quorum) / span : 0.0;
            normalized = normalized * (1.0 - soft) + 0.5 * soft;
            v = quorum + normalized * span;
        }

        state->vote[i] = clamp01(v);
    }
}

