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

static void write_temp_config(const char *path, const char *hmac_key) {
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp,
            "{\"run_id\":\"verify-test\",\"fractal_map\":\"configs/fractal_map.default.json\","
            "\"dataset\":\"datasets/demo.csv\",\"fmt\":5,\"lambda\":0.01,\"seed\":4242,"
            "\"salt\":\"verify-salt\",\"hmac_key\":\"%s\"}",
            hmac_key);
    fclose(fp);
}

int main(void) {
    const char *cfg_path = "configs/verify_temp.json";
    const char *log_path = "logs/verify_chain.jsonl";
    remove(cfg_path);
    remove(log_path);
    write_temp_config(cfg_path, "super-secret-key");

    kolibri_runtime_t runtime;
    assert(kolibri_runtime_init(&runtime, cfg_path) == 0);
    kolibri_chain_t chain;
    kolibri_chain_init(&chain);
    assert(kolibri_run_with_callback(&runtime,
                                     5,
                                     1,
                                     runtime.config.lambda_default,
                                     runtime.config.fmt_default,
                                     log_path,
                                     &chain,
                                     NULL,
                                     NULL) == 0);
    kolibri_chain_free(&chain);
    kolibri_config_t good_cfg = runtime.config;
    kolibri_runtime_free(&runtime);
    assert(kolibri_verify_file(log_path, &good_cfg, 0) == 0);

    kolibri_config_t bad_cfg = good_cfg;
    strncpy(bad_cfg.hmac_key, "wrong-key", sizeof(bad_cfg.hmac_key) - 1);
    bad_cfg.hmac_key[sizeof(bad_cfg.hmac_key) - 1] = '\0';
    assert(kolibri_verify_file(log_path, &bad_cfg, 0) != 0);

    flip_byte(log_path);
    assert(kolibri_verify_file(log_path, &good_cfg, 0) != 0);

    remove(cfg_path);
    remove(log_path);
    return 0;
}
