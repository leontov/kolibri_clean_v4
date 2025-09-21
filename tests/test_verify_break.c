#include "core.h"
#include "fmt_v5.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    fmt_init_locale();
    const char *path = "tests/tmp_chain.jsonl";
    ReasonBlock block;
    memset(&block, 0, sizeof(block));
    block.step = 0;
    block.parent = -1;
    block.seed = 777;
    strcpy(block.formula, "mul(c0,x)");
    block.eff = 0.42;
    block.compl = 4.0;
    block.prev[0] = '\0';
    for (size_t i = 0; i < KOLIBRI_VOTE_COUNT; i++) {
        block.votes[i] = 0.05 * (double)(i + 1);
    }
    block.fmt = 5;
    strcpy(block.fa, "7056172034");
    block.fa_stab = 0;
    strcpy(block.fa_map, "demo.json");
    block.r = 0.5;
    strcpy(block.run_id, "demo");
    strcpy(block.cfg_hash, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    block.eff_train = 0.5;
    block.eff_val = 0.4;
    block.eff_test = 0.3;
    strcpy(block.explain, "test block");
    block.hmac_alg[0] = '\0';
    block.salt[0] = '\0';

    char payload[1024];
    char record[1536];
    size_t payload_len = 0;
    size_t record_len = 0;
    if (fmt_build_json(&block, 0, NULL, NULL, payload, sizeof(payload), &payload_len) != 0) {
        fprintf(stderr, "payload build failed\n");
        return 1;
    }
    core_compute_sha256_hex((unsigned char *)payload, payload_len, block.hash_hex);
    block.hmac_hex[0] = '\0';
    if (fmt_build_json(&block, 1, block.hash_hex, block.hmac_hex, record, sizeof(record), &record_len) != 0) {
        fprintf(stderr, "record build failed\n");
        return 1;
    }
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "file open failed\n");
        return 1;
    }
    fwrite(record, 1, record_len, fp);
    fputc('\n', fp);
    fclose(fp);

    size_t blocks = 0;
    size_t failed = 0;
    char err[128];
    if (core_verify_chain(path, NULL, &blocks, &failed, err, sizeof(err)) != 0) {
        fprintf(stderr, "unexpected verify failure: %s\n", err);
        remove(path);
        return 1;
    }
    FILE *mut = fopen(path, "r+");
    if (!mut) {
        fprintf(stderr, "mut open failed\n");
        remove(path);
        return 1;
    }
    int c = fgetc(mut);
    if (c == EOF) {
        fclose(mut);
        remove(path);
        return 1;
    }
    fseek(mut, 0, SEEK_SET);
    fputc((c == '{') ? '[' : '{', mut);
    fclose(mut);

    if (core_verify_chain(path, NULL, &blocks, &failed, err, sizeof(err)) == 0) {
        fprintf(stderr, "verify should have failed\n");
        remove(path);
        return 1;
    }
    if (failed == 0) {
        fprintf(stderr, "failed step not reported\n");
        remove(path);
        return 1;
    }
    remove(path);
    printf("OK\n");
    return 0;
}
