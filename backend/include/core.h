#ifndef KOLIBRI_CORE_H
#define KOLIBRI_CORE_H

#include <stddef.h>
#include <stdint.h>

#define KOLIBRI_FIELD_LEN 96
#define KOLIBRI_HASH_HEX 64
#define KOLIBRI_HASH_TEXT (KOLIBRI_HASH_HEX + 1)
#define KOLIBRI_VOTE_COUNT 10
#define KOLIBRI_FORMULA_LEN 512
#define KOLIBRI_EXPLAIN_LEN 80

typedef struct {
    char run_id[64];
    uint64_t base_seed;
    char dataset_path[256];
    char fa_map_path[256];
    char output_path[256];
    size_t steps_default;
    double lambda_default;
    int fmt_default;
    int beam_default;
    size_t fa_window;
    char salt[64];
} KolibriConfig;

typedef struct {
    double *xs;
    double *ys;
    size_t count;
} Dataset;

typedef struct ReasonBlock {
    int step;
    int parent;
    uint64_t seed;
    char formula[KOLIBRI_FORMULA_LEN];
    double eff;
    double compl;
    char prev[KOLIBRI_HASH_TEXT];
    double votes[KOLIBRI_VOTE_COUNT];
    int fmt;
    char fa[KOLIBRI_VOTE_COUNT + 1];
    int fa_stab;
    char fa_map[KOLIBRI_FIELD_LEN];
    double r;
    char run_id[64];
    char cfg_hash[KOLIBRI_HASH_TEXT];
    double eff_train;
    double eff_val;
    double eff_test;
    char explain[KOLIBRI_EXPLAIN_LEN + 1];
    char hmac_alg[32];
    char salt[64];
    char hash_hex[KOLIBRI_HASH_TEXT];
    char hmac_hex[KOLIBRI_HASH_TEXT];
} ReasonBlock;

int core_load_config(const char *path, KolibriConfig *cfg, char *cfg_hash_hex, char *err, size_t err_size);

int core_run(const KolibriConfig *cfg,
             const char *cfg_hash,
             size_t steps,
             int beam,
             double lambda,
             int fmt_id,
             const char *output_path);

int core_verify_chain(const char *path,
                      const char *hmac_key,
                      size_t *out_blocks,
                      size_t *failed_step,
                      char *err,
                      size_t err_size);

int core_replay_chain(const char *path, char *hash_out, size_t hash_out_size, char *err, size_t err_size);

void core_compute_sha256_hex(const unsigned char *data, size_t len, char *hex_out);
void core_compute_hmac_sha256_hex(const unsigned char *data, size_t len, const char *key, char *hex_out);

#endif /* KOLIBRI_CORE_H */
