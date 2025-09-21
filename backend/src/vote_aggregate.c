#include "vote_aggregate.h"

#include <math.h>
#include <stddef.h>
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

VotePolicy vote_policy_from_config(const kolibri_config_t* cfg) {
    VotePolicy policy = {1.0, 0.0, 0.0};
    if (cfg) {
        policy.depth_decay = cfg->depth_decay;
        policy.quorum = cfg->quorum;
        policy.temperature = cfg->temperature;
    }
    policy.depth_decay = clamp01(policy.depth_decay);
    policy.quorum = clamp01(policy.quorum);
    policy.temperature = clamp01(policy.temperature);
    return policy;
}

static void apply_votes(double votes[10], double quorum, double smoothing) {
    double span = 1.0 - quorum;
    for (size_t i = 0; i < 10; ++i) {
        double v = clamp01(votes[i]);
        if (v < quorum) {
            votes[i] = 0.0;
            continue;
        }
        if (smoothing > 0.0) {
            double normalized = span > 0.0 ? (v - quorum) / span : 0.0;
            normalized = normalized * (1.0 - smoothing) + 0.5 * smoothing;
            v = quorum + normalized * span;
        }
        votes[i] = clamp01(v);
    }
}

void vote_apply_policy(VoteState* state, const VotePolicy* policy) {
    if (!state) {
        return;
    }

    double votes[10];
    for (size_t i = 0; i < 10; ++i) {
        votes[i] = state->vote[i];
    }

    double depth_decay = 1.0;
    double quorum = 0.0;
    double smoothing = clamp01(state->temperature);

    if (policy) {
        depth_decay = clamp01(policy->depth_decay);
        quorum = clamp01(policy->quorum);
        smoothing = clamp01(policy->temperature);
    }

    for (size_t i = 0; i < 10; ++i) {
        double v = clamp01(votes[i]);
        if (depth_decay > 0.0) {
            v = depth_decay * v + (1.0 - depth_decay) * 0.5;
        }
        votes[i] = v;
    }

    apply_votes(votes, quorum, smoothing);

    for (size_t i = 0; i < 10; ++i) {
        state->vote[i] = votes[i];
    }
}

void vote_apply_policy_values(double votes[10], const VotePolicy* policy) {
    if (!votes) {
        return;
    }

    double quorum = 0.0;
    double smoothing = 0.0;
    if (policy) {
        quorum = clamp01(policy->quorum);
        smoothing = clamp01(policy->temperature);
    }

    apply_votes(votes, quorum, smoothing);
}

void digit_layers_aggregate(double out[10], const VotePolicy* policy,
                            const double layers[][10], size_t layer_count) {
    if (!out) {
        return;
    }

    memset(out, 0, sizeof(double) * 10);
    if (!layers || layer_count == 0) {
        return;
    }

    double decay = 1.0;
    if (policy) {
        decay = clamp01(policy->depth_decay);
    }

    double total_weight = 0.0;
    for (size_t depth = 0; depth < layer_count; ++depth) {
        double weight;
        if (depth == 0) {
            weight = 1.0;
        } else if (decay == 0.0) {
            weight = 0.0;
        } else {
            weight = pow(decay, (double)depth);
        }
        if (weight == 0.0) {
            continue;
        }
        for (size_t digit = 0; digit < 10; ++digit) {
            out[digit] += layers[depth][digit] * weight;
        }
        total_weight += weight;
    }

    if (total_weight > 0.0) {
        for (size_t digit = 0; digit < 10; ++digit) {
            out[digit] = clamp01(out[digit] / total_weight);
        }
    }
}
