#ifndef KOLIBRI_PERSIST_H
#define KOLIBRI_PERSIST_H

#include <stddef.h>
#include <stdint.h>

#include "chain.h"
#include "metrics.h"

typedef struct {
    uint32_t   step;
    KolMetrics metrics;
    double     dataset_mean;
    double     dataset_min;
    double     dataset_max;
} KolPersistState;

const char *persist_chain_path(void);
uint64_t    persist_timestamp(void);
void        persist_hash_block(const KolBlock *block, uint8_t out[32]);
int         persist_append_block(const KolBlock *block);
int         persist_load_blocks(KolBlock **blocks, size_t *count);
const char *persist_state_path(void);
int         persist_save_state(const KolPersistState *state);
int         persist_load_state(KolPersistState *state);

#endif
