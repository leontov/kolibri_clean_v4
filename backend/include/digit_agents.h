#ifndef KOLIBRI_DIGIT_AGENTS_H
#define KOLIBRI_DIGIT_AGENTS_H

#include "common.h"

typedef struct {
    int step;               // current step index (>= 1)
    uint64_t seed;          // deterministic seed shared across agents
    double quorum;          // quorum threshold supplied by the core
} AgentContext;

typedef struct {
    double vote[10];
} VoteState;

double agent_vote(const AgentContext* ctx, int agent_id);
void digit_votes(const AgentContext* ctx, VoteState* out);

#endif
