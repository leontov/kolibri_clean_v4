#ifndef KOLIBRI_VOTE_AGGREGATE_H
#define KOLIBRI_VOTE_AGGREGATE_H

#include "digit_agents.h"

typedef struct {
    double depth_decay;   // contribution from deeper hierarchy layers (0..1)
    double quorum;        // activation threshold
} VotePolicy;

void vote_apply_policy(VoteState* state, const VotePolicy* policy);

#endif
