#include "core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void flip_byte(const char *path) {
    FILE *fp = fopen(path, "r+");
    assert(fp != NULL);
    int c = fgetc(fp);
    assert(c != EOF);
    fseek(fp, 0, SEEK_SET);
    c ^= 0x1;
    fputc(c, fp);
    fclose(fp);
}

int main(void) {
    kolibri_runtime_t runtime;
    assert(kolibri_runtime_init(&runtime, "configs/kolibri.json") == 0);
    kolibri_chain_t chain;
    kolibri_chain_init(&chain);
    assert(kolibri_run_with_callback(&runtime, 5, 1, runtime.config.lambda_default, runtime.config.fmt_default, "logs/verify_chain.jsonl", &chain, NULL, NULL) == 0);
    kolibri_chain_free(&chain);
    kolibri_runtime_free(&runtime);
    assert(kolibri_verify_file("logs/verify_chain.jsonl", 0) == 0);
    flip_byte("logs/verify_chain.jsonl");
    assert(kolibri_verify_file("logs/verify_chain.jsonl", 0) != 0);
    return 0;
}
