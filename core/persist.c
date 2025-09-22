#include "persist.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *CHAIN_PATH = "kolibri_chain.jsonl";
#if defined(__wasi__)
static KolBlock wasm_blocks[512];
static size_t   wasm_count = 0;
#endif

const char *persist_chain_path(void) {
    return CHAIN_PATH;
}

uint64_t persist_timestamp(void) {
#if defined(__wasi__)
    static uint64_t fake = 0;
    return ++fake;
#else
    return (uint64_t)time(NULL);
#endif
}

static uint64_t mix64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

void persist_hash_block(const KolBlock *block, uint8_t out[32]) {
    if (!block || !out) {
        return;
    }
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    state ^= mix64((uint64_t)block->step + ((uint64_t)block->digit_id << 32));
    state ^= mix64(block->ts);
    uint64_t eff_q = (uint64_t)llround(block->eff * 1e6);
    uint64_t compl_q = (uint64_t)llround(block->compl * 1e6);
    state ^= mix64(eff_q);
    state ^= mix64(compl_q);
    for (size_t i = 0; i < sizeof(block->formula); ++i) {
        state ^= mix64((uint64_t)(unsigned char)block->formula[i] + i);
    }
    for (size_t i = 0; i < 32; ++i) {
        state ^= mix64((uint64_t)block->prev[i] + i * 131);
        uint64_t m = mix64(state + (uint64_t)i * 0x12345ULL);
        out[i] = (uint8_t)(m & 0xFFu);
    }
}

static void hex_encode(const uint8_t *data, size_t len, char *out, size_t out_len) {
    static const char HEX[] = "0123456789abcdef";
    if (out_len < len * 2 + 1) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = HEX[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = HEX[data[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static int hex_decode(const char *hex, uint8_t *out, size_t len) {
    size_t hex_len = strlen(hex);
    if (hex_len < len * 2) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        int hv = isdigit((unsigned char)hi) ? hi - '0' : 10 + (tolower((unsigned char)hi) - 'a');
        int lv = isdigit((unsigned char)lo) ? lo - '0' : 10 + (tolower((unsigned char)lo) - 'a');
        out[i] = (uint8_t)((hv << 4) | lv);
    }
    return 0;
}

int persist_append_block(const KolBlock *block) {
    if (!block) {
        return -1;
    }
#if defined(__wasi__)
    if (wasm_count < sizeof(wasm_blocks) / sizeof(wasm_blocks[0])) {
        wasm_blocks[wasm_count++] = *block;
        return 0;
    }
    return -1;
#else
    FILE *fp = fopen(persist_chain_path(), "ab");
    if (!fp) {
        return -1;
    }
    char hash_hex[65];
    char prev_hex[65];
    hex_encode(block->hash, 32, hash_hex, sizeof(hash_hex));
    hex_encode(block->prev, 32, prev_hex, sizeof(prev_hex));
    int wrote = fprintf(fp,
                        "{\"step\":%u,\"digit\":%u,\"formula\":\"%s\",\"eff\":%.6f,\"compl\":%.6f,\"ts\":%llu,\"hash\":\"%s\",\"prev\":\"%s\"}\n",
                        block->step, block->digit_id, block->formula, block->eff, block->compl,
                        (unsigned long long)block->ts, hash_hex, prev_hex);
    fclose(fp);
    return wrote > 0 ? 0 : -1;
#endif
}

int persist_load_blocks(KolBlock **blocks, size_t *count) {
    if (!blocks || !count) {
        return -1;
    }
    *blocks = NULL;
    *count = 0;
#if defined(__wasi__)
    if (wasm_count == 0) {
        return 0;
    }
    KolBlock *tmp = (KolBlock *)malloc(wasm_count * sizeof(KolBlock));
    if (!tmp) {
        return -1;
    }
    for (size_t i = 0; i < wasm_count; ++i) {
        tmp[i] = wasm_blocks[i];
    }
    *blocks = tmp;
    *count = wasm_count;
    return 0;
#else
    FILE *fp = fopen(persist_chain_path(), "rb");
    if (!fp) {
        return 0;
    }
    char line[1024];
    size_t capacity = 0;
    while (fgets(line, sizeof(line), fp)) {
        KolBlock block;
        memset(&block, 0, sizeof(block));
        char formula[256];
        char hash_hex[65];
        char prev_hex[65];
        unsigned int step = 0;
        unsigned int digit = 0;
        double eff = 0.0;
        double compl = 0.0;
        unsigned long long ts = 0ULL;
        if (sscanf(line,
                   "{\"step\":%u,\"digit\":%u,\"formula\":\"%255[^\"]\",\"eff\":%lf,\"compl\":%lf,\"ts\":%llu,\"hash\":\"%64[^\"]\",\"prev\":\"%64[^\"]\"}",
                   &step, &digit, formula, &eff, &compl, &ts, hash_hex, prev_hex) == 8) {
            block.step = step;
            block.digit_id = (uint8_t)digit;
            size_t len = strlen(formula);
            if (len >= sizeof(block.formula)) {
                len = sizeof(block.formula) - 1;
            }
            memcpy(block.formula, formula, len);
            block.formula[len] = '\0';
            block.eff = eff;
            block.compl = compl;
            block.ts = (uint64_t)ts;
            hex_decode(hash_hex, block.hash, 32);
            hex_decode(prev_hex, block.prev, 32);
            if (*count >= capacity) {
                size_t new_cap = capacity ? capacity * 2 : 8;
                KolBlock *tmp = (KolBlock *)realloc(*blocks, new_cap * sizeof(KolBlock));
                if (!tmp) {
                    fclose(fp);
                    free(*blocks);
                    *blocks = NULL;
                    *count = 0;
                    return -1;
                }
                *blocks = tmp;
                capacity = new_cap;
            }
            (*blocks)[(*count)++] = block;
        }
    }
    fclose(fp);
    return 0;
#endif
}
