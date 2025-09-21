#include "core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    kolibri_runtime_t runtime;
    assert(kolibri_runtime_init(&runtime, "configs/kolibri.json") == 0);
    kolibri_chain_t chain;
    kolibri_chain_init(&chain);
    assert(kolibri_run_with_callback(&runtime, 1, 1, runtime.config.lambda_default, runtime.config.fmt_default, "logs/test_chain.jsonl", &chain, NULL, NULL) == 0);
    kolibri_chain_free(&chain);
    kolibri_runtime_free(&runtime);
    FILE *fp = fopen("logs/test_chain.jsonl", "r");
    assert(fp != NULL);
    char line[4096];
    assert(fgets(line, sizeof(line), fp) != NULL);
    fclose(fp);
    assert(strstr(line, "\"payload\":") != NULL);
    assert(strstr(line, "\"hash\":") != NULL);
    assert(strstr(line, "\"hmac\":") != NULL);
    return 0;
}
