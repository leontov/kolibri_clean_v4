#include "chain.h"

#include <stdlib.h>
#include <string.h>

#include "persist.h"

int chain_append(const KolBlock *block) {
    if (!block) {
        return -1;
    }
    KolBlock copy = *block;
    KolBlock *existing = NULL;
    size_t count = 0;
    if (persist_load_blocks(&existing, &count) != 0) {
        return -1;
    }
    if (count > 0) {
        memcpy(copy.prev, existing[count - 1].hash, sizeof(copy.prev));
    } else {
        memset(copy.prev, 0, sizeof(copy.prev));
    }
    free(existing);
    persist_hash_block(&copy, copy.hash);
    if (persist_append_block(&copy) != 0) {
        return -1;
    }
    return 0;
}

int chain_tail(KolBlock *out, int n) {
    if (!out || n <= 0) {
        return -1;
    }
    KolBlock *existing = NULL;
    size_t count = 0;
    if (persist_load_blocks(&existing, &count) != 0) {
        return -1;
    }
    if ((size_t)n > count) {
        n = (int)count;
    }
    for (int i = 0; i < n; ++i) {
        out[i] = existing[count - (size_t)n + (size_t)i];
    }
    free(existing);
    return n;
}

int chain_verify(void) {
    KolBlock *blocks = NULL;
    size_t count = 0;
    if (persist_load_blocks(&blocks, &count) != 0) {
        return -1;
    }
    uint8_t prev[32] = {0};
    for (size_t i = 0; i < count; ++i) {
        KolBlock expected = blocks[i];
        uint8_t hash[32];
        memcpy(expected.prev, prev, sizeof(prev));
        persist_hash_block(&expected, hash);
        if (memcmp(hash, blocks[i].hash, sizeof(hash)) != 0) {
            free(blocks);
            return -1;
        }
        memcpy(prev, blocks[i].hash, sizeof(prev));
    }
    free(blocks);
    return 0;
}
