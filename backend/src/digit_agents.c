#include "digit_agents.h"
#include <math.h>
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

static uint64_t splitmix64(uint64_t x) {
    x += UINT64_C(0x9E3779B97F4A7C15);
    x = (x ^ (x >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94D049BB133111EB);
    return x ^ (x >> 31);
}

static double unit_from_u64(uint64_t x) {
    return (double)(x >> 11) * (1.0 / 9007199254740992.0);
}

static double base_pattern(int agent_id, int step) {
    const double harmonics[10] = {
        0.73, 1.17, 0.93, 1.41, 0.66,
        1.07, 0.81, 1.23, 0.58, 1.37
    };
    const double offsets[10] = {
        0.0, 0.35, 0.62, 0.18, 0.47,
        0.73, 0.28, 0.92, 0.11, 0.56
    };
    double freq = harmonics[agent_id];
    double shift = offsets[agent_id];
    double phase = (double)step * freq + shift;
    double wave = sin(phase);
    return 0.5 + 0.5 * wave;
}

double agent_vote(const AgentContext* ctx, int agent_id) {
    if (!ctx || agent_id < 0 || agent_id >= 10) {
        return 0.0;
    }

    uint64_t seed = ctx->seed;
    seed ^= (uint64_t)(ctx->step + 1) * UINT64_C(0xA0761D6478BD642F);
    seed ^= (uint64_t)(agent_id + 1) * UINT64_C(0xE7037ED1A0B428DB);
    uint64_t noise_src = splitmix64(seed);
    double noise = unit_from_u64(noise_src);

    double pattern = base_pattern(agent_id, ctx->step);
    double influence = 0.65 * pattern + 0.35 * noise;

    if (ctx->quorum > 0.0) {
        double guard = clamp01(ctx->quorum * 0.5);
        influence = guard + (1.0 - guard) * influence;
    }

    return clamp01(influence);
}

void digit_votes(const AgentContext* ctx, VoteState* out) {
    if (!out) {
        return;
    }
    memset(out->vote, 0, sizeof(out->vote));
    if (!ctx) {
        return;
    }
    for (int i = 0; i < 10; ++i) {
        out->vote[i] = agent_vote(ctx, i);
    }
}

