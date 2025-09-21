#include "core.h"
#include "fmt_v5.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    fmt_init_locale();
    ReasonBlock block;
    memset(&block, 0, sizeof(block));
    block.step = 1;
    block.parent = 0;
    block.seed = 42;
    strcpy(block.formula, "add(x,c0)");
    block.eff = 0.5;
    block.compl = 3.0;
    block.prev[0] = '\0';
    for (size_t i = 0; i < KOLIBRI_VOTE_COUNT; i++) {
        block.votes[i] = 0.1 * (double)i;
    }
    block.fmt = 5;
    strcpy(block.fa, "1234567890");
    block.fa_stab = 2;
    strcpy(block.fa_map, "demo.json");
    block.r = 0.75;
    strcpy(block.run_id, "demo");
    strcpy(block.cfg_hash, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    block.eff_train = 0.9;
    block.eff_val = 0.8;
    block.eff_test = 0.7;
    strcpy(block.explain, "payload snapshot");
    block.hmac_alg[0] = '\0';
    strcpy(block.salt, "s");
    block.hash_hex[0] = '\0';
    block.hmac_hex[0] = '\0';

    char payload[1024];
    size_t payload_len = 0;
    if (fmt_build_json(&block, 0, NULL, NULL, payload, sizeof(payload), &payload_len) != 0) {
        fprintf(stderr, "fmt_build_json failed\n");
        return 1;
    }
    const char *expected =
        "{\"step\":1,\"parent\":0,\"seed\":42,\"formula\":\"add(x,c0)\",\"eff\":0.5,\"compl\":3,"
        "\"prev\":\"\",\"votes\":[0,0.10000000000000001,0.20000000000000001,0.30000000000000004,0.40000000000000002,0.5,0.60000000000000009,0.70000000000000007,0.80000000000000004,0.90000000000000002],\"fmt\":5,\"fa\":\"1234567890\","
        "\"fa_stab\":2,\"fa_map\":\"demo.json\",\"r\":0.75,\"run_id\":\"demo\",\"cfg_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"eff_train\":0.90000000000000002,\"eff_val\":0.80000000000000004,\"eff_test\":0.69999999999999996,\"explain\":\"payload snapshot\",\"hmac_alg\":\"\",\"salt\":\"s\"}";
    if (strcmp(payload, expected) != 0) {
        fprintf(stderr, "payload mismatch\n");
        fprintf(stderr, "got: %s\n", payload);
        fprintf(stderr, "exp: %s\n", expected);
        return 1;
    }
    core_compute_sha256_hex((unsigned char *)payload, payload_len, block.hash_hex);
    block.hmac_hex[0] = '\0';
    char record[1536];
    size_t record_len = 0;
    if (fmt_build_json(&block, 1, block.hash_hex, block.hmac_hex, record, sizeof(record), &record_len) != 0) {
        fprintf(stderr, "fmt_build_json record failed\n");
        return 1;
    }
    if (strstr(record, "\"hash\":\"") == NULL) {
        fprintf(stderr, "hash missing in record\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}
