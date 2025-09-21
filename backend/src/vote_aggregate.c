#include "vote_aggregate.h"
#include <string.h>

static double clamp01(double x){
    if(x < 0.0) return 0.0;
    if(x > 1.0) return 1.0;
    return x;
}

VotePolicy vote_policy_from_config(const kolibri_config_t* cfg){
    VotePolicy policy = {1.0, 0.0, 0.0};
    if(cfg){
        policy.depth_decay = cfg->depth_decay;
        policy.quorum = cfg->quorum;
        policy.temperature = cfg->temperature;
    }
    if(policy.depth_decay < 0.0) policy.depth_decay = 0.0;
    if(policy.depth_decay > 1.0) policy.depth_decay = 1.0;
    if(policy.quorum < 0.0) policy.quorum = 0.0;
    if(policy.quorum > 1.0) policy.quorum = 1.0;
    if(policy.temperature < 0.0) policy.temperature = 0.0;
    if(policy.temperature > 1.0) policy.temperature = 1.0;
    return policy;
}

void vote_apply_policy(double votes[10], const VotePolicy* policy){
    if(!votes) return;
    double quorum = 0.0;
    double temperature = 0.0;
    if(policy){
        quorum = policy->quorum;
        temperature = policy->temperature;
    }
    if(quorum < 0.0) quorum = 0.0;
    if(quorum > 1.0) quorum = 1.0;
    if(temperature < 0.0) temperature = 0.0;
    if(temperature > 1.0) temperature = 1.0;

    double scale = 1.0 - temperature;
    if(scale < 0.0) scale = 0.0;

    for(int i = 0; i < 10; ++i){
        double value = votes[i];
        if(value < quorum){
            votes[i] = 0.0;
            continue;
        }
        double centered = value - 0.5;
        votes[i] = clamp01(0.5 + centered * scale);
    }
}
