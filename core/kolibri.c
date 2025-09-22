#include "kolibri.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "persist.h"
#include "dsl.h"
#include "language.h"

static KolEngine *g_engine = NULL;
static KolEvent   g_event;
static int        g_has_event = 0;
static KolLanguage g_language;

int kol_init(uint8_t depth, uint32_t seed) {
    kol_reset();
    g_engine = engine_create(depth, seed);
    g_has_event = 0;
    language_reset(&g_language);
    if (!g_engine) {
        return -1;
    }
    return 0;
}

void kol_reset(void) {
    if (g_engine) {
        engine_free(g_engine);
        g_engine = NULL;
    }
    g_has_event = 0;
    language_reset(&g_language);
}

int kol_tick(void) {
    if (!g_engine) {
        return -1;
    }
    KolEvent *event_ptr = g_has_event ? &g_event : NULL;
    int res = engine_tick(g_engine, event_ptr, NULL);
    g_has_event = 0;
    return res;
}

int kol_chat_push(const char *text) {
    if (!g_engine) {
        return -1;
    }
    if (!text) {
        return -1;
    }
    language_observe(&g_language, text);
    if (engine_ingest_text(g_engine, text, &g_event) != 0) {
        return -1;
    }
    g_has_event = 1;
    return 0;
}

int kol_bootstrap(int steps, KolBootstrapReport *report) {
    if (!g_engine || steps <= 0) {
        return -1;
    }
    KolBootstrapReport local;
    memset(&local, 0, sizeof(local));
    local.start_step = g_engine->step;
    double best_eff = -1e9;
    for (int i = 0; i < steps; ++i) {
        if (kol_tick() != 0) {
            return -1;
        }
        double eff = g_engine->last.eff;
        if (eff > best_eff) {
            best_eff = eff;
            local.best_eff = eff;
            local.best_compl = g_engine->last.compl;
            local.best_step = g_engine->step;
            char *formula = dsl_print(g_engine->current);
            if (formula) {
                strncpy(local.best_formula, formula, sizeof(local.best_formula) - 1);
                local.best_formula[sizeof(local.best_formula) - 1] = '\0';
                free(formula);
            } else {
                local.best_formula[0] = '\0';
            }
        }
    }
    local.executed = g_engine->step - local.start_step;
    local.final_eff = g_engine->last.eff;
    local.final_compl = g_engine->last.compl;
    if (report) {
        *report = local;
    }
    return 0;
}

double kol_eff(void) {
    if (!g_engine) {
        return 0.0;
    }
    return g_engine->last.eff;
}

double kol_compl(void) {
    if (!g_engine) {
        return 0.0;
    }
    return g_engine->last.compl;
}

static void hex_encode(const uint8_t *data, size_t len, char *out, size_t cap) {
    static const char HEX[] = "0123456789abcdef";
    if (cap < len * 2 + 1) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = HEX[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = HEX[data[i] & 0xF];
    }
    out[len * 2] = '\0';
}

int kol_tail_json(char *buf, int cap, int n) {
    if (!buf || cap <= 0 || n <= 0) {
        return -1;
    }
    KolBlock *blocks = (KolBlock *)malloc((size_t)n * sizeof(KolBlock));
    if (!blocks) {
        return -1;
    }
    int got = chain_tail(blocks, n);
    if (got < 0) {
        free(blocks);
        return -1;
    }
    buf[0] = '\0';
    int used = snprintf(buf, (size_t)cap, "[");
    if (used < 0 || used >= cap) {
        free(blocks);
        return -1;
    }
    int offset = used;
    for (int i = 0; i < got; ++i) {
        char hash_hex[65];
        char prev_hex[65];
        hex_encode(blocks[i].hash, 32, hash_hex, sizeof(hash_hex));
        hex_encode(blocks[i].prev, 32, prev_hex, sizeof(prev_hex));
        int written = snprintf(buf + offset, (size_t)(cap - offset),
                               "%s{\"step\":%u,\"digit\":%u,\"formula\":\"%s\",\"eff\":%.6f,\"compl\":%.6f,\"ts\":%llu,\"hash\":\"%s\",\"prev\":\"%s\"}",
                               i == 0 ? "" : ",",
                               blocks[i].step, blocks[i].digit_id, blocks[i].formula,
                               blocks[i].eff, blocks[i].compl,
                               (unsigned long long)blocks[i].ts, hash_hex, prev_hex);
        if (written < 0 || written >= cap - offset) {
            free(blocks);
            return -1;
        }
        offset += written;
    }
    int tail = snprintf(buf + offset, (size_t)(cap - offset), "]");
    if (tail < 0 || tail >= cap - offset) {
        free(blocks);
        return -1;
    }
    offset += tail;
    free(blocks);
    return offset;
}

void *kol_alloc(size_t size) {
    return malloc(size);
}

void kol_free(void *ptr) {
    free(ptr);
}

int kol_language_generate(char *buf, size_t cap) {
    return language_generate(&g_language, buf, cap);
}
