#ifndef KOLIBRI_VOTE_AGGREGATE_H
#define KOLIBRI_VOTE_AGGREGATE_H

#include "digit_agents.h"
#include "config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double depth_decay;
    double quorum;
    double temperature;
} VotePolicy;

VotePolicy vote_policy_from_config(const kolibri_config_t* cfg);

void vote_apply_policy(VoteState* state, const VotePolicy* policy);

void vote_apply_policy_values(double votes[10], const VotePolicy* policy);

void digit_layers_aggregate(double out[10], const VotePolicy* policy,
                            const double layers[][10], size_t layer_count);

#ifdef __cplusplus
}
#endif

#endif
