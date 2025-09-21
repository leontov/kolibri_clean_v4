#ifndef KOLIBRI_VOTE_AGGREGATE_H
#define KOLIBRI_VOTE_AGGREGATE_H

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

void digit_aggregate(double out[10], const VotePolicy* policy,
                     const double layers[][10], size_t layer_count);

void vote_apply_policy(double votes[10], const VotePolicy* policy);

#ifdef __cplusplus
}
#endif

#endif
