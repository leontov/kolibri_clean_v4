#include "core.h"
#include "dsl.h"
#include "fractal.h"
#include "fmt_v5.h"
#define JSMN_STATIC
#include "jsmn.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int token_equals(const char *json, const jsmntok_t *tok, const char *s) {
    size_t len = (size_t)(tok->end - tok->start);
    return strlen(s) == len && strncmp(json + tok->start, s, len) == 0;
}

static int skip_token(const jsmntok_t *tokens, int index) {
    int j = index;
    if (tokens[j].type == JSMN_OBJECT || tokens[j].type == JSMN_ARRAY) {
        int count = tokens[j].size;
        j++;
        for (int i = 0; i < count; i++) {
            j = skip_token(tokens, j);
        }
        return j;
    }
    return j + 1;
}

static int copy_token_string(const char *json, const jsmntok_t *tok, char *out, size_t out_size) {
    size_t len = (size_t)(tok->end - tok->start);
    if (len + 1 > out_size) {
        return -1;
    }
    memcpy(out, json + tok->start, len);
    out[len] = '\0';
    return 0;
}

static double parse_token_double(const char *json, const jsmntok_t *tok) {
    char buf[128];
    size_t len = (size_t)(tok->end - tok->start);
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, json + tok->start, len);
    buf[len] = '\0';
    return strtod(buf, NULL);
}

static long long parse_token_int(const char *json, const jsmntok_t *tok) {
    return (long long)parse_token_double(json, tok);
}

static uint64_t parse_token_uint64(const char *json, const jsmntok_t *tok) {
    char buf[128];
    size_t len = (size_t)(tok->end - tok->start);
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, json + tok->start, len);
    buf[len] = '\0';
    return (uint64_t)strtoull(buf, NULL, 10);
}

static int ensure_directories(const char *path) {
    char temp[512];
    size_t len = strlen(path);
    if (len >= sizeof(temp)) {
        return -1;
    }
    strcpy(temp, path);
    char *slash = strrchr(temp, '/');
    if (slash == NULL) {
        return 0;
    }
    *slash = '\0';
    if (temp[0] == '\0') {
        return 0;
    }
    char partial[512];
    partial[0] = '\0';
    size_t partial_len = 0;
    for (size_t i = 0; i < strlen(temp); i++) {
        partial[partial_len++] = temp[i];
        partial[partial_len] = '\0';
        if (temp[i] == '/') {
            if (partial_len > 0 && mkdir(partial, 0770) != 0) {
                if (errno != EEXIST) {
                    return -1;
                }
            }
        }
    }
    if (mkdir(temp, 0770) != 0) {
        if (errno != EEXIST) {
            return -1;
        }
    }
    return 0;
}

static int read_file(const char *path, char **out_data, size_t *out_len) {
    *out_data = NULL;
    *out_len = 0;
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    char *data = (char *)malloc((size_t)size + 1);
    if (data == NULL) {
        fclose(fp);
        return -1;
    }
    size_t read = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    data[read] = '\0';
    *out_data = data;
    *out_len = read;
    return 0;
}

int core_load_config(const char *path, KolibriConfig *cfg, char *cfg_hash_hex, char *err, size_t err_size) {
    if (cfg == NULL) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "cfg null");
        }
        return -1;
    }
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->run_id, "kolibri-demo");
    cfg->base_seed = 1337;
    strcpy(cfg->dataset_path, "datasets/demo.csv");
    strcpy(cfg->fa_map_path, "configs/fractal_map.default.json");
    strcpy(cfg->output_path, "logs/chain.jsonl");
    cfg->steps_default = 30;
    cfg->lambda_default = 0.01;
    cfg->fmt_default = 5;
    cfg->beam_default = 1;
    cfg->fa_window = 5;
    cfg->salt[0] = '\0';

    char *json = NULL;
    size_t len = 0;
    if (read_file(path, &json, &len) != 0) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "cannot read %s", path);
        }
        return -1;
    }
    if (cfg_hash_hex != NULL) {
        core_compute_sha256_hex((unsigned char *)json, len, cfg_hash_hex);
    }

    jsmn_parser parser;
    jsmn_init(&parser);
    const unsigned int max_tokens = 256;
    jsmntok_t *tokens = (jsmntok_t *)calloc(max_tokens, sizeof(jsmntok_t));
    if (tokens == NULL) {
        free(json);
        if (err && err_size > 0) {
            snprintf(err, err_size, "alloc tokens");
        }
        return -1;
    }
    int parsed = jsmn_parse(&parser, json, len, tokens, max_tokens);
    if (parsed < 0 || tokens[0].type != JSMN_OBJECT) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "invalid config");
        }
        free(tokens);
        free(json);
        return -1;
    }
    int idx = 1;
    for (int i = 0; i < tokens[0].size; i++) {
        jsmntok_t *key = &tokens[idx++];
        jsmntok_t *val = &tokens[idx];
        if (token_equals(json, key, "run_id")) {
            copy_token_string(json, val, cfg->run_id, sizeof(cfg->run_id));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "base_seed")) {
            cfg->base_seed = parse_token_uint64(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "dataset")) {
            copy_token_string(json, val, cfg->dataset_path, sizeof(cfg->dataset_path));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "fa_map")) {
            copy_token_string(json, val, cfg->fa_map_path, sizeof(cfg->fa_map_path));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "output")) {
            copy_token_string(json, val, cfg->output_path, sizeof(cfg->output_path));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "steps")) {
            cfg->steps_default = (size_t)parse_token_uint64(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "lambda")) {
            cfg->lambda_default = parse_token_double(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "fmt")) {
            cfg->fmt_default = (int)parse_token_int(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "beam")) {
            cfg->beam_default = (int)parse_token_int(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "fa_window")) {
            cfg->fa_window = (size_t)parse_token_uint64(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "salt")) {
            copy_token_string(json, val, cfg->salt, sizeof(cfg->salt));
            idx = skip_token(tokens, idx);
        } else {
            idx = skip_token(tokens, idx);
        }
    }

    free(tokens);
    free(json);
    return 0;
}

static int load_dataset(const char *path, Dataset *dataset) {
    dataset->xs = NULL;
    dataset->ys = NULL;
    dataset->count = 0;
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    size_t capacity = 32;
    dataset->xs = (double *)malloc(capacity * sizeof(double));
    dataset->ys = (double *)malloc(capacity * sizeof(double));
    if (dataset->xs == NULL || dataset->ys == NULL) {
        fclose(fp);
        free(dataset->xs);
        free(dataset->ys);
        dataset->xs = dataset->ys = NULL;
        return -1;
    }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        char *comma = strchr(line, ',');
        if (comma == NULL) {
            continue;
        }
        *comma = '\0';
        char *x_str = line;
        char *y_str = comma + 1;
        if (!strchr(x_str, '.') && !strchr(x_str, '-') && !isdigit((unsigned char)x_str[0])) {
            continue;
        }
        double x = strtod(x_str, NULL);
        double y = strtod(y_str, NULL);
        if (dataset->count >= capacity) {
            capacity *= 2;
            double *new_xs = (double *)realloc(dataset->xs, capacity * sizeof(double));
            double *new_ys = (double *)realloc(dataset->ys, capacity * sizeof(double));
            if (new_xs == NULL || new_ys == NULL) {
                free(dataset->xs);
                free(dataset->ys);
                dataset->xs = dataset->ys = NULL;
                fclose(fp);
                return -1;
            }
            dataset->xs = new_xs;
            dataset->ys = new_ys;
        }
        dataset->xs[dataset->count] = x;
        dataset->ys[dataset->count] = y;
        dataset->count++;
    }
    fclose(fp);
    if (dataset->count == 0) {
        free(dataset->xs);
        free(dataset->ys);
        dataset->xs = dataset->ys = NULL;
        return -1;
    }
    return 0;
}

static void free_dataset(Dataset *dataset) {
    free(dataset->xs);
    free(dataset->ys);
    dataset->xs = dataset->ys = NULL;
    dataset->count = 0;
}

static double segment_mse(const Dataset *dataset, size_t start, size_t end, const DSLNode *formula, const double *params, size_t param_count) {
    if (end <= start || end > dataset->count) {
        return 0.0;
    }
    double mse = 0.0;
    size_t count = end - start;
    for (size_t i = start; i < end; i++) {
        double x = dataset->xs[i];
        double y = dataset->ys[i];
        double pred = dsl_eval(formula, x, params, param_count);
        double diff = pred - y;
        mse += diff * diff;
    }
    return mse / (double)count;
}

static void compute_metrics(const Dataset *dataset, const DSLNode *formula, const double *params, size_t param_count, double *mse_train, double *mse_val, double *mse_test) {
    size_t n = dataset->count;
    size_t train_end = (size_t)((double)n * 0.6);
    size_t val_end = (size_t)((double)n * 0.8);
    if (train_end == 0) {
        train_end = n > 0 ? 1 : 0;
    }
    if (val_end <= train_end) {
        val_end = train_end < n ? train_end + 1 : train_end;
    }
    if (val_end > n) {
        val_end = n;
    }
    *mse_train = segment_mse(dataset, 0, train_end, formula, params, param_count);
    *mse_val = segment_mse(dataset, train_end, val_end, formula, params, param_count);
    *mse_test = segment_mse(dataset, val_end, n, formula, params, param_count);
}

static const char *basename_path(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash != NULL && slash[1] != '\0') {
        return slash + 1;
    }
    return path;
}

int core_run(const KolibriConfig *cfg,
             const char *cfg_hash,
             size_t steps,
             int beam,
             double lambda,
             int fmt_id,
             const char *output_path) {
    if (cfg == NULL || cfg_hash == NULL) {
        return -1;
    }
    KolibriConfig local = *cfg;
    if (steps == 0) {
        steps = local.steps_default;
    }
    if (beam <= 0) {
        beam = local.beam_default;
    }
    if (lambda <= 0.0) {
        lambda = local.lambda_default;
    }
    if (fmt_id <= 0) {
        fmt_id = local.fmt_default;
    }
    const char *out_path = output_path && output_path[0] ? output_path : local.output_path;

    if (ensure_directories(out_path) != 0) {
        return -1;
    }

    Dataset dataset;
    if (load_dataset(local.dataset_path, &dataset) != 0) {
        return -1;
    }

    FractalMap map;
    char map_err[128];
    if (fractal_load_map(local.fa_map_path, &map, map_err, sizeof(map_err)) != 0) {
        free_dataset(&dataset);
        return -1;
    }

    size_t window_size = local.fa_window > 0 ? local.fa_window : map.stab_window;
    if (window_size == 0) {
        window_size = 5;
    }
    if (window_size > 5) {
        window_size = 5;
    }

    FILE *fp = fopen(out_path, "w");
    if (fp == NULL) {
        free_dataset(&dataset);
        return -1;
    }

    const char *hmac_key_env = getenv("KOLIBRI_HMAC_KEY");
    const char *hmac_key = (hmac_key_env && hmac_key_env[0]) ? hmac_key_env : NULL;

    char prev_hash[KOLIBRI_HASH_TEXT];
    prev_hash[0] = '\0';
    char window[5][KOLIBRI_VOTE_COUNT + 1];
    size_t window_count = 0;

    for (size_t step = 0; step < steps; step++) {
        ReasonBlock block;
        memset(&block, 0, sizeof(block));
        block.step = (int)step;
        block.parent = (step == 0) ? -1 : (int)step - 1;
        block.seed = local.base_seed ^ (0x9e3779b97f4a7c15ULL * (step + 1));
        fractal_generate_votes(block.seed, block.votes);
        fractal_votes_to_address(block.votes, block.fa);

        if (window_count >= window_size) {
            window_count = window_size - 1;
        }
        for (size_t i = window_count; i > 0; i--) {
            memcpy(window[i], window[i - 1], KOLIBRI_VOTE_COUNT + 1);
        }
        memcpy(window[0], block.fa, KOLIBRI_VOTE_COUNT + 1);
        if (window_count < window_size) {
            window_count++;
        }
        block.fa_stab = (int)fractal_stability((const char (*)[KOLIBRI_FA_DIGITS + 1])window, window_count);

        DSLNode *formula = fractal_build_formula(&map, block.fa, block.votes);
        if (formula == NULL) {
            fclose(fp);
            free_dataset(&dataset);
            return -1;
        }
        dsl_print_canonical(formula, block.formula, sizeof(block.formula));

        double params[3];
        params[0] = map.r * block.votes[1];
        params[1] = 0.5 + block.votes[2];
        params[2] = block.votes[3] * 2.0 - 1.0;

        double mse_train = 0.0;
        double mse_val = 0.0;
        double mse_test = 0.0;
        compute_metrics(&dataset, formula, params, 3, &mse_train, &mse_val, &mse_test);
        block.eff_train = 1.0 / (1.0 + mse_train);
        block.eff_val = 1.0 / (1.0 + mse_val);
        block.eff_test = 1.0 / (1.0 + mse_test);

        block.compl = (double)dsl_complexity(formula);
        block.eff = block.eff_val - lambda * block.compl;

        snprintf(block.explain, sizeof(block.explain), "score=%.5f beam=%d", block.eff, beam);
        strncpy(block.prev, prev_hash, sizeof(block.prev) - 1);
        block.prev[sizeof(block.prev) - 1] = '\0';
        block.fmt = fmt_id;
        strncpy(block.fa_map, basename_path(local.fa_map_path), sizeof(block.fa_map) - 1);
        block.fa_map[sizeof(block.fa_map) - 1] = '\0';
        block.r = map.r;
        strncpy(block.run_id, local.run_id, sizeof(block.run_id) - 1);
        block.run_id[sizeof(block.run_id) - 1] = '\0';
        strncpy(block.cfg_hash, cfg_hash, sizeof(block.cfg_hash) - 1);
        block.cfg_hash[sizeof(block.cfg_hash) - 1] = '\0';
        strncpy(block.salt, local.salt, sizeof(block.salt) - 1);
        block.salt[sizeof(block.salt) - 1] = '\0';
        if (hmac_key) {
            strncpy(block.hmac_alg, "HMAC_SHA256", sizeof(block.hmac_alg) - 1);
        } else {
            block.hmac_alg[0] = '\0';
        }

        char payload[4096];
        char record[4096];
        size_t payload_len = 0;
        size_t record_len = 0;
        if (fmt_build_json(&block, 0, NULL, NULL, payload, sizeof(payload), &payload_len) != 0) {
            dsl_free(formula);
            fclose(fp);
            free_dataset(&dataset);
            return -1;
        }

        core_compute_sha256_hex((unsigned char *)payload, payload_len, block.hash_hex);
        if (hmac_key) {
            core_compute_hmac_sha256_hex((unsigned char *)payload, payload_len, hmac_key, block.hmac_hex);
        } else {
            block.hmac_hex[0] = '\0';
        }

        if (fmt_build_json(&block, 1, block.hash_hex, block.hmac_hex, record, sizeof(record), &record_len) != 0) {
            dsl_free(formula);
            fclose(fp);
            free_dataset(&dataset);
            return -1;
        }

        if (fwrite(record, 1, record_len, fp) != record_len || fputc('\n', fp) == EOF) {
            dsl_free(formula);
            fclose(fp);
            free_dataset(&dataset);
            return -1;
        }

        strncpy(prev_hash, block.hash_hex, sizeof(prev_hash) - 1);
        prev_hash[sizeof(prev_hash) - 1] = '\0';

        dsl_free(formula);
    }

    fclose(fp);
    free_dataset(&dataset);
    return 0;
}
