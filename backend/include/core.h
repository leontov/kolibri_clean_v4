#ifndef KOLIBRI_CORE_H
#define KOLIBRI_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "dsl.h"
#include "fractal.h"

#define KOLIBRI_VERSION "1.0.0"
#define KOLIBRI_DEFAULT_LOG "logs/chain.jsonl"

typedef struct {
    char run_id[64];
    char cfg_path[256];
    char fractal_map_path[256];
    char dataset_path[256];
    unsigned fmt_default;
    double lambda_default;
    uint64_t seed_default;
    char salt[64];
    char hmac_key[65];
} kolibri_config_t;

typedef struct {
    double x;
    double y;
} dataset_point_t;

typedef struct {
    dataset_point_t *points;
    size_t length;
} dataset_t;

typedef struct kolibri_payload {
    unsigned step;
    int parent;
    uint64_t seed;
    kolibri_formula_t formula;
    double eff;
    double compl;
    char prev[65];
    double votes[FA10_LENGTH];
    unsigned fmt;
    char fa[FA10_LENGTH + 1];
    size_t fa_stab;
    char *fa_map;
    double r;
    char run_id[64];
    char cfg_hash[65];
    double eff_train;
    double eff_val;
    double eff_test;
    char explain[81];
    char hmac_alg[16];
    char salt[64];
} kolibri_payload_t;

typedef struct kolibri_block {
    kolibri_payload_t payload;
    char hash[65];
    char hmac[65];
} kolibri_block_t;

typedef struct kolibri_chain {
    kolibri_block_t *blocks;
    size_t count;
    size_t capacity;
} kolibri_chain_t;

typedef struct kolibri_runtime {
    kolibri_config_t config;
    fractal_map_t fractal;
    dataset_t dataset;
} kolibri_runtime_t;

int kolibri_load_config(const char *path, kolibri_config_t *cfg);
int kolibri_runtime_init(kolibri_runtime_t *rt, const char *cfg_path);
void kolibri_runtime_free(kolibri_runtime_t *rt);

int dataset_load(const char *path, dataset_t *dataset);
void dataset_free(dataset_t *dataset);

void kolibri_chain_init(kolibri_chain_t *chain);
void kolibri_chain_free(kolibri_chain_t *chain);
int kolibri_chain_push(kolibri_chain_t *chain, const kolibri_block_t *block);
int kolibri_chain_tail_copy(const kolibri_chain_t *chain, size_t tail, kolibri_chain_t *out);

void kolibri_hash_payload(const kolibri_payload_t *payload, char out_hex[65]);
void kolibri_hmac_payload(const kolibri_payload_t *payload, const char *key, char out_hex[65]);

typedef void (*kolibri_event_callback)(const char *event, const char *json, void *user_data);

int kolibri_run_with_callback(kolibri_runtime_t *rt,
                              unsigned steps,
                              unsigned beam,
                              double lambda_override,
                              unsigned fmt,
                              const char *log_path,
                              kolibri_chain_t *out_chain,
                              kolibri_event_callback cb,
                              void *cb_data);

int kolibri_verify_file(const char *path, const kolibri_config_t *cfg, int verbose);
int kolibri_replay(const kolibri_config_t *cfg);

int kolibri_parse_block_line(const char *line, size_t len, kolibri_payload_t *payload, char *hash_hex, char *hmac_hex);

#endif
