#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "digit.h"
#include "persist.h"

static void init_dataset(KolEngine *engine) {
    size_t n = sizeof(engine->xs) / sizeof(engine->xs[0]);
    for (size_t i = 0; i < n; ++i) {
        double t = -1.0 + 2.0 * (double)i / (double)(n - 1);
        engine->xs[i] = t;
        engine->ys[i] = sin(t * 3.141592653589793);
    }
    engine->dataset.xs = engine->xs;
    engine->dataset.ys = engine->ys;
    engine->dataset.count = n;
}

KolEngine *engine_create(uint8_t depth, uint32_t seed) {
    KolEngine *engine = (KolEngine *)calloc(1, sizeof(KolEngine));
    if (!engine) {
        return NULL;
    }
    init_dataset(engine);
    engine->fractal = fractal_create(depth, seed);
    if (!engine->fractal) {
        free(engine);
        return NULL;
    }
    uint32_t rng_state = seed ? seed : 1234u;
    engine->current = dsl_rand(&rng_state, 3);
    engine->last = metrics_eval(engine->current, &engine->dataset);
    engine->step = 0;
    return engine;
}

void engine_free(KolEngine *engine) {
    if (!engine) {
        return;
    }
    dsl_free(engine->current);
    fractal_free(engine->fractal);
    free(engine);
}

static KolDigit *get_digit(KolDigit *root, uint8_t idx) {
    if (!root) {
        return NULL;
    }
    if (root->children[idx]) {
        return root->children[idx];
    }
    return root;
}

static KolFormula *choose_candidate(KolEngine *engine, KolDigit *leader_digit, uint8_t leader_id) {
    uint32_t local_state = leader_digit ? leader_digit->rng.state : (engine->step + 1u) * 811u;
    KolFormula *candidate = NULL;
    if (engine->current && leader_id < 4) {
        candidate = dsl_mutate(engine->current, &local_state);
    } else if (engine->current && leader_id < 7) {
        candidate = dsl_simplify(engine->current);
    } else {
        candidate = dsl_rand(&local_state, 3);
    }
    if (!candidate && engine->current) {
        candidate = dsl_clone(engine->current);
    }
    return candidate;
}

int engine_tick(KolEngine *engine, const KolEvent *in, KolOutput *out) {
    if (!engine) {
        return -1;
    }
    if (in && in->length > 0) {
        size_t count = engine->dataset.count;
        for (size_t i = 0; i < count && i < in->length; ++i) {
            engine->ys[i] = -1.0 + 2.0 * ((double)in->digits[i] / 9.0);
        }
    }
    KolDigit *root = fractal_root(engine->fractal);
    KolDigit *digits[10];
    for (uint8_t i = 0; i < 10; ++i) {
        digits[i] = get_digit(root, i);
    }
    KolState state = {engine->current, engine->last, engine->step};
    KolVote vote = vote_run(digits, &state);
    KolDigit *leader_digit = digits[vote.leader_id];
    KolFormula *candidate = choose_candidate(engine, leader_digit, vote.leader_id);
    if (!candidate) {
        return -1;
    }
    KolMetrics cand_metrics = metrics_eval(candidate, &engine->dataset);
    engine->step += 1;
    int adopt = 0;
    if (cand_metrics.eff >= engine->last.eff || vote.scores[vote.leader_id] > 0.7f) {
        adopt = 1;
    }
    if (adopt) {
        dsl_free(engine->current);
        engine->current = candidate;
        engine->last = cand_metrics;
        if (leader_digit) {
            digit_learn(leader_digit, engine->current, &engine->last);
        }
    } else {
        dsl_free(candidate);
    }
    KolBlock block;
    memset(&block, 0, sizeof(block));
    block.step = engine->step;
    block.digit_id = vote.leader_id;
    char *formula_str = dsl_print(engine->current);
    if (formula_str) {
        strncpy(block.formula, formula_str, sizeof(block.formula) - 1);
        block.formula[sizeof(block.formula) - 1] = '\0';
        free(formula_str);
    }
    block.eff = engine->last.eff;
    block.compl = engine->last.compl;
    block.ts = persist_timestamp();
    chain_append(&block);
    if (out) {
        strncpy(out->formula, block.formula, sizeof(out->formula) - 1);
        out->formula[sizeof(out->formula) - 1] = '\0';
        out->metrics = engine->last;
        out->leader = vote.leader_id;
    }
    return 0;
}

int engine_ingest_text(KolEngine *engine, const char *utf8, KolEvent *out_event) {
    (void)engine;
    if (!out_event || !utf8) {
        return -1;
    }
    size_t len = 0;
    while (utf8[len] && len < sizeof(out_event->digits)) {
        unsigned char ch = (unsigned char)utf8[len];
        out_event->digits[len] = (uint8_t)(ch % 10u);
        ++len;
    }
    out_event->length = len;
    return 0;
}
