#include "digit_agents.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint64_t next_u64(uint64_t* state) {
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * UINT64_C(2685821657736338717);
}

static double prng01(uint64_t* state) {
    return (next_u64(state) >> 11) * (1.0 / 9007199254740992.0);
}

static void free_agent(DigitAgent* a);

static DigitAgent* create_agent(int depth, int depth_max, uint64_t* seed) {
    DigitAgent* a = (DigitAgent*)calloc(1, sizeof(DigitAgent));
    if (!a) {
        return NULL;
    }
    uint64_t local = *seed + (uint64_t)depth * UINT64_C(0x9E3779B185EBCA87);
    a->seed = next_u64(&local);
    double noise = prng01(&a->seed);
    a->weight = 0.45 + 0.1 * (double)depth + 0.45 * noise;
    if (a->weight > 1.0) {
        a->weight = 1.0;
    }
    if (a->weight < 0.0) {
        a->weight = 0.0;
    }

    if (depth < depth_max) {
        for (int i = 0; i < 10; ++i) {
            uint64_t child_seed = a->seed ^ (uint64_t)i * UINT64_C(0xC2B2AE3D27D4EB4F);
            a->sub[i] = create_agent(depth + 1, depth_max, &child_seed);
            if (!a->sub[i] && depth + 1 <= depth_max) {
                // allocation failed, clean up and abort
                for (int j = 0; j < i; ++j) {
                    if (a->sub[j]) {
                        free_agent(a->sub[j]);
                        a->sub[j] = NULL;
                    }
                }
                free(a);
                return NULL;
            }
        }
    }
    return a;
}

static void free_agent(DigitAgent* a) {
    if (!a) {
        return;
    }
    for (int i = 0; i < 10; ++i) {
        free_agent(a->sub[i]);
    }
    free(a);
}

static void tick_agent(DigitAgent* a, double signal, int depth) {
    if (!a) {
        return;
    }
    uint64_t state = a->seed;
    double noise = prng01(&state);
    a->seed = state;

    double target = noise;
    if (signal >= 0.0) {
        double mix = 0.6 + 0.1 * fmin((double)depth, 4.0);
        if (mix > 0.95) {
            mix = 0.95;
        }
        target = mix * signal + (1.0 - mix) * noise;
    }

    double inertia = 0.65 + 0.05 * (double)(depth > 0 ? 1 : 0);
    if (inertia > 0.95) {
        inertia = 0.95;
    }
    a->weight = inertia * a->weight + (1.0 - inertia) * target;
    if (a->weight < 0.0) {
        a->weight = 0.0;
    } else if (a->weight > 1.0) {
        a->weight = 1.0;
    }

    for (int i = 0; i < 10; ++i) {
        tick_agent(a->sub[i], signal, depth + 1);
    }
}

static void accumulate(const DigitAgent* a, int depth, double* sum, double* norm) {
    if (!a) {
        return;
    }
    double factor = 1.0 / (double)(depth + 1);
    *sum += a->weight * factor;
    *norm += factor;
    for (int i = 0; i < 10; ++i) {
        accumulate(a->sub[i], depth + 1, sum, norm);
    }
}

bool digit_field_init(DigitField* f, int depth_max, uint64_t seed) {
    if (!f || depth_max < 0) {
        return false;
    }
    memset(f, 0, sizeof(*f));
    f->depth_max = depth_max;
    for (int i = 0; i < 10; ++i) {
        uint64_t local_seed = seed ^ ((uint64_t)i * UINT64_C(0xA0761D6478BD642F));
        f->root[i] = create_agent(0, depth_max, &local_seed);
        if (!f->root[i]) {
            digit_field_free(f);
            return false;
        }
    }
    return true;
}

void digit_field_free(DigitField* f) {
    if (!f) {
        return;
    }
    for (int i = 0; i < 10; ++i) {
        free_agent(f->root[i]);
        f->root[i] = NULL;
    }
    f->depth_max = 0;
}

void digit_tick(DigitField* f, const double external[10]) {
    if (!f) {
        return;
    }
    for (int i = 0; i < 10; ++i) {
        double signal = external ? external[i] : -1.0;
        if (signal > 1.0) {
            signal = 1.0;
        }
        if (signal < 0.0 && external) {
            signal = 0.0;
        }
        tick_agent(f->root[i], signal, 0);
    }
}

void digit_aggregate(const DigitField* f, VoteState* out) {
    if (!f || !out) {
        return;
    }
    for (int i = 0; i < 10; ++i) {
        out->vote[i] = 0.0;
    }
    double total = 0.0;
    for (int i = 0; i < 10; ++i) {
        double sum = 0.0;
        double norm = 0.0;
        accumulate(f->root[i], 0, &sum, &norm);
        if (norm > 0.0) {
            out->vote[i] = sum / norm;
        } else {
            out->vote[i] = 0.0;
        }
        total += out->vote[i];
    }
    if (total > 0.0) {
        for (int i = 0; i < 10; ++i) {
            out->vote[i] /= total;
        }
    }
}
