#ifndef KOLIBRI_VOTE_AGGREGATE_H
#define KOLIBRI_VOTE_AGGREGATE_H

#include "digit_agents.h"

typedef struct {
    double depth_decay;   // blending against neutral prior (0..1)
    double quorum;        // zero-out threshold
    double temperature;   // softening factor (0..1)
} VotePolicy;

void vote_apply_policy(VoteState* state, const VotePolicy* policy);

#endif
