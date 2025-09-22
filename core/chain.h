#ifndef KOLIBRI_CHAIN_H
#define KOLIBRI_CHAIN_H

#include <stdint.h>

#include "metrics.h"

typedef struct {
    uint32_t step;
    uint8_t digit_id;
    char formula[256];
    double eff;
    double compl;
    uint64_t ts;
    uint8_t hash[32];
    uint8_t prev[32];
} KolBlock;

int chain_append(const KolBlock *block);
int chain_tail(KolBlock *out, int n);
int chain_verify(void);

#endif
