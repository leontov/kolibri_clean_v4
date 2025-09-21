
#include "digit_agents.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define DIGIT_BRANCHING 10
#define DIGIT_TICK_MIX 0.65
#define DIGIT_CHILD_BLEND 0.35
#define DIGIT_AGGREGATE_DECAY 0.6

static uint64_t splitmix64(uint64_t x) {
    x += UINT64_C(0x9E3779B97F4A7C15);
    x = (x ^ (x >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94D049BB133111EB);
    return x ^ (x >> 31);
}

static double unit_from_u64(uint64_t x) {
    return (double)(x >> 11) * (1.0 / 9007199254740992.0);
}

static void digit_agent_free(DigitAgent* agent) {
    if (!agent) {
        return;
    }
    for (size_t i = 0; i < DIGIT_BRANCHING; ++i) {
        digit_agent_free(agent->sub[i]);
        agent->sub[i] = NULL;
    }
    free(agent);
}

static DigitAgent* digit_agent_clone(int depth, int depth_max, uint64_t seed) {
    DigitAgent* agent = calloc(1, sizeof(DigitAgent));
    if (!agent) {
        return NULL;
    }

    agent->seed = seed;
    agent->weight = unit_from_u64(splitmix64(seed));

    if (depth + 1 >= depth_max) {
        return agent;
    }

    for (size_t i = 0; i < DIGIT_BRANCHING; ++i) {
        uint64_t child_seed = splitmix64(seed ^ (UINT64_C(0xA0761D6478BD642F) * (i + 1)));
        agent->sub[i] = digit_agent_clone(depth + 1, depth_max, child_seed);
        if (!agent->sub[i]) {
            digit_agent_free(agent);
            return NULL;
        }
    }

    return agent;
}

bool digit_field_init(DigitField* field, int depth_max, uint64_t seed) {
    if (!field || depth_max <= 0) {
        return false;
    }

    memset(field, 0, sizeof(*field));
    field->depth_max = depth_max;

    for (size_t i = 0; i < DIGIT_BRANCHING; ++i) {
        uint64_t branch_seed = splitmix64(seed + (uint64_t)(i + 1));
        field->root[i] = digit_agent_clone(0, depth_max, branch_seed);
        if (!field->root[i]) {
            digit_field_free(field);
            return false;
        }
    }

    return true;
}

void digit_field_free(DigitField* field) {
    if (!field) {
        return;
    }
    for (size_t i = 0; i < DIGIT_BRANCHING; ++i) {
        digit_agent_free(field->root[i]);
        field->root[i] = NULL;
    }
    field->depth_max = 0;
}

static void digit_agent_tick(DigitAgent* agent) {
    if (!agent) {
        return;
    }

    agent->seed = splitmix64(agent->seed);
    double sample = unit_from_u64(agent->seed);

    double child_sum = 0.0;
    int child_count = 0;
    for (size_t i = 0; i < DIGIT_BRANCHING; ++i) {
        if (!agent->sub[i]) {
            continue;
        }
        digit_agent_tick(agent->sub[i]);
        child_sum += agent->sub[i]->weight;
        child_count++;
    }

    double child_avg = child_count > 0 ? (child_sum / (double)child_count) : sample;
    double updated = DIGIT_TICK_MIX * sample + DIGIT_CHILD_BLEND * child_avg;
    double current = 1.0 - DIGIT_TICK_MIX - DIGIT_CHILD_BLEND;
    if (current < 0.0) {
        current = 0.0;
    }
    agent->weight = current * agent->weight + updated;

    if (agent->weight < 0.0) {
        agent->weight = 0.0;
    } else if (agent->weight > 1.0) {
        agent->weight = 1.0;
    }
}

void digit_tick(DigitField* field) {
    if (!field) {
        return;
    }
    for (size_t i = 0; i < DIGIT_BRANCHING; ++i) {
        digit_agent_tick(field->root[i]);
    }
}

static void accumulate_vote(const DigitAgent* agent, double weight, double* sum, double* norm) {
    if (!agent) {
        return;
    }
    *sum += weight * agent->weight;
    *norm += weight;
    double child_weight = weight * DIGIT_AGGREGATE_DECAY;
    for (size_t i = 0; i < DIGIT_BRANCHING; ++i) {
        if (agent->sub[i]) {
            accumulate_vote(agent->sub[i], child_weight, sum, norm);
        }
    }
}

void digit_aggregate(const DigitField* field, VoteState* out) {
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!field) {
        return;
    }

    for (size_t i = 0; i < DIGIT_BRANCHING; ++i) {
        double sum = 0.0;
        double norm = 0.0;
        accumulate_vote(field->root[i], 1.0, &sum, &norm);
        if (norm > 0.0) {
            double value = sum / norm;
            if (value < 0.0) {
                value = 0.0;
            } else if (value > 1.0) {
                value = 1.0;
            }
            out->vote[i] = value;
        } else {
            out->vote[i] = 0.0;
        }
    }
}

=======
#include "vote_aggregate.h"
#include <math.h>
#include <string.h>

static double clamp01(double x){
    if(x < 0.0) return 0.0;
    if(x > 1.0) return 1.0;
    return x;
}

void digit_aggregate(double out[10], const VotePolicy* policy,
                     const double layers[][10], size_t layer_count){
    if(!out) return;
    memset(out, 0, sizeof(double) * 10);
    if(layer_count == 0 || !layers) return;

    double decay = 1.0;
    if(policy){
        decay = policy->depth_decay;
    }
    if(decay < 0.0) decay = 0.0;
    if(decay > 1.0) decay = 1.0;

    double total_weight = 0.0;
    for(size_t depth = 0; depth < layer_count; ++depth){
        double weight;
        if(depth == 0){
            weight = 1.0;
        }else if(decay == 0.0){
            weight = 0.0;
        }else{
            weight = pow(decay, (double)depth);
        }
        if(weight == 0.0) continue;
        for(int digit = 0; digit < 10; ++digit){
            out[digit] += layers[depth][digit] * weight;
        }
        total_weight += weight;
    }

    if(total_weight > 0.0){
        for(int digit = 0; digit < 10; ++digit){
            out[digit] = clamp01(out[digit] / total_weight);
        }
    }
}

