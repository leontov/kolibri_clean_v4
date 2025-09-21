#ifndef KOLIBRI_VOTE_AGGREGATE_H
#define KOLIBRI_VOTE_AGGREGATE_H

#include "digit_agents.h"

typedef struct {
    double depth_decay;
    double quorum;
} VotePolicy;

void vote_apply_policy(VoteState* state, const VotePolicy* policy);

#endif
