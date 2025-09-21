#include "fractal.h"
#include "reason.h"
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_fractal_addr_deterministic(void){
    double votes[10] = {0.05, 0.15, 0.95, 0.33, 0.51, 0.72, 0.41, 0.08, 0.67, 0.2};
    char fa1[11];
    char fa2[11];
    fractal_address_from_votes(votes, fa1);
    fractal_address_from_votes(votes, fa2);
    if(strcmp(fa1, fa2) != 0){
        fprintf(stderr, "deterministic address mismatch\n");
        exit(1);
    }
    if(strcmp(fa1, "0193564162") != 0){
        fprintf(stderr, "unexpected address %s\n", fa1);
        exit(1);
    }
}

static void test_fractal_prefix_stability(void){
    const char history[5][11] = {
        "7056172034",
        "7056172031",
        "7056179034",
        "7056172034",
        "7056172034"
    };
    int prefix = fractal_common_prefix_len(history, 5);
    if(prefix != 6){
        fprintf(stderr, "expected prefix length 6, got %d\n", prefix);
        exit(1);
    }
}

static void test_roundtrip(void){
    ReasonBlock block;
    memset(&block, 0, sizeof(block));
    block.step = 1;
    block.parent = 0;
    block.seed = 42;
    snprintf(block.config_fingerprint, sizeof(block.config_fingerprint), "%s", "cafebabe");
    snprintf(block.fmt, sizeof(block.fmt), "%s", "dsl-v1");
    snprintf(block.formula, sizeof(block.formula), "%s", "x");
    block.eff = 0.5;
    block.compl = 1.0;
    strncpy(block.prev, "", sizeof(block.prev));
    for(int i = 0; i < 10; ++i){
        block.votes[i] = 0.1 * (double)i;
        block.bench_eff[i] = 0.2 * (double)(i + 1);
    }
    block.vote_softmax = 0.3;
    block.vote_median = 0.25;
    snprintf(block.memory, sizeof(block.memory), "%s", "memo");
    snprintf(block.merkle, sizeof(block.merkle), "%s", "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    snprintf(block.fa, sizeof(block.fa), "%s", "7056172034");
    block.fa_stab = 6;
    snprintf(block.fa_map, sizeof(block.fa_map), "%s", "default_v1");
    block.fractal_r = 0.5;

    char payload1[4096];
    int len1 = rb_payload_json(&block, payload1, sizeof(payload1));
    if(len1 <= 0){
        fprintf(stderr, "failed to serialize payload1\n");
        exit(1);
    }
    unsigned char digest1[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)payload1, (size_t)len1, digest1);

    block.fa[0] = (block.fa[0] == '9') ? '8' : '9';
    char payload2[4096];
    int len2 = rb_payload_json(&block, payload2, sizeof(payload2));
    if(len2 <= 0){
        fprintf(stderr, "failed to serialize payload2\n");
        exit(1);
    }
    unsigned char digest2[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)payload2, (size_t)len2, digest2);

    if(memcmp(digest1, digest2, SHA256_DIGEST_LENGTH) == 0){
        fprintf(stderr, "hash should change when fractal address changes\n");
        exit(1);
    }
}

int main(void){
    test_fractal_addr_deterministic();
    test_fractal_prefix_stability();
    test_roundtrip();
    return 0;
}
