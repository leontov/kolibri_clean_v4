#ifndef KOLIBRI_PERSIST_H
#define KOLIBRI_PERSIST_H

#include <stddef.h>
#include <stdint.h>

#include "chain.h"

const char *persist_chain_path(void);
uint64_t    persist_timestamp(void);
void        persist_hash_block(const KolBlock *block, uint8_t out[32]);
void        persist_quantize_formula(char *formula, size_t cap);
int         persist_append_block(const KolBlock *block);
int         persist_load_blocks(KolBlock **blocks, size_t *count);

#endif
