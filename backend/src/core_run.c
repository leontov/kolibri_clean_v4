#include "core.h"

#include "fmt_v5.h"

#include <errno.h>
#include <math.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static double prng_uniform(uint64_t *state) {
    return (double)(xorshift64(state) & 0xFFFFFF) / (double)0xFFFFFF;
}

static int ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        return -1;
    }
#if defined(_WIN32)
    return _mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

static int compute_file_hash(const char *path, char out_hex[65]) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        SHA256_Update(&ctx, buffer, n);
    }
    fclose(fp);
    SHA256_Final(digest, &ctx);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        out_hex[i * 2] = hex[(digest[i] >> 4) & 0xF];
        out_hex[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    out_hex[SHA256_DIGEST_LENGTH * 2] = '\0';
    return 0;
}

static void broadcast_event(kolibri_event_callback cb, void *cb_data, const char *event, const char *json) {
    if (cb) {
        cb(event, json, cb_data);
    }
}

int kolibri_run_with_callback(kolibri_runtime_t *rt,
                              unsigned steps,
                              unsigned beam,
                              double lambda_override,
                              unsigned fmt,
                              const char *log_path,
                              kolibri_chain_t *out_chain,
                              kolibri_event_callback cb,
                              void *cb_data) {
    if (!rt || steps == 0) {
        return -1;
    }
    fmt_init_locale();
    if (!fmt) {
        fmt = rt->config.fmt_default;
    }
    if (lambda_override <= 0.0) {
        lambda_override = rt->config.lambda_default;
    }
    if (!log_path) {
        log_path = KOLIBRI_DEFAULT_LOG;
    }
    if (ensure_directory("logs") != 0) {
        fprintf(stderr, "failed to create logs directory\n");
        return -1;
    }
    FILE *fp = fopen(log_path, "w");
    if (!fp) {
        fprintf(stderr, "failed to open %s: %s\n", log_path, strerror(errno));
        return -1;
    }
    kolibri_chain_t local_chain;
    kolibri_chain_t *target_chain = out_chain ? out_chain : &local_chain;
    if (!out_chain) {
        kolibri_chain_init(&local_chain);
    }
    char cfg_hash[65] = {0};
    if (compute_file_hash(rt->config.cfg_path, cfg_hash) != 0) {
        strcpy(cfg_hash, "");
    }
    uint64_t state = rt->config.seed_default;
    char prev_hash[65] = "";
    char fa_history[256][FA10_LENGTH + 1];
    size_t history_len = 0;

    for (unsigned step = 0; step < steps; ++step) {
        double votes[FA10_LENGTH];
        for (size_t i = 0; i < FA10_LENGTH; ++i) {
            votes[i] = prng_uniform(&state);
        }
        kolibri_formula_t formula;
        dsl_build_formula(votes, &formula);

        size_t train_len = (size_t)((double)rt->dataset.length * 0.6);
        size_t val_len = (size_t)((double)rt->dataset.length * 0.2);
        if (train_len + val_len > rt->dataset.length) {
            val_len = 0;
        }
        size_t test_len = rt->dataset.length - train_len - val_len;
        double mse_train = 0.0, mse_val = 0.0, mse_test = 0.0;
        size_t idx = 0;
        for (; idx < train_len; ++idx) {
            double pred = dsl_evaluate(&formula, rt->dataset.points[idx].x);
            double diff = pred - rt->dataset.points[idx].y;
            mse_train += diff * diff;
        }
        for (size_t j = 0; j < val_len; ++j, ++idx) {
            double pred = dsl_evaluate(&formula, rt->dataset.points[idx].x);
            double diff = pred - rt->dataset.points[idx].y;
            mse_val += diff * diff;
        }
        for (; idx < rt->dataset.length; ++idx) {
            double pred = dsl_evaluate(&formula, rt->dataset.points[idx].x);
            double diff = pred - rt->dataset.points[idx].y;
            mse_test += diff * diff;
        }
        if (train_len) {
            mse_train /= (double)train_len;
        }
        if (val_len) {
            mse_val /= (double)val_len;
        }
        if (test_len) {
            mse_test /= (double)test_len;
        }
        double eff_train = dsl_compute_eff(mse_train);
        double eff_val = dsl_compute_eff(val_len ? mse_val : mse_train);
        double eff_test = dsl_compute_eff(test_len ? mse_test : mse_val);
        double eff = eff_val;
        double compl = dsl_compute_complexity_score(formula.node_count) + lambda_override * (double)beam;

        kolibri_payload_t payload;
        memset(&payload, 0, sizeof(payload));
        payload.step = step;
        payload.parent = (step == 0) ? -1 : (int)(step - 1);
        payload.seed = state;
        payload.formula = formula;
        payload.eff = eff;
        payload.compl = compl;
        strncpy(payload.prev, prev_hash, sizeof(payload.prev) - 1);
        memcpy(payload.votes, votes, sizeof(votes));
        payload.fmt = fmt;
        fractal_encode_votes(votes, payload.fa);
        if (history_len < 256) {
            strncpy(fa_history[history_len], payload.fa, FA10_LENGTH + 1);
            history_len += 1;
        } else {
            memmove(fa_history, fa_history + 1, (255) * (FA10_LENGTH + 1));
            strncpy(fa_history[255], payload.fa, FA10_LENGTH + 1);
        }
        size_t window_len = history_len < 256 ? history_len : 256;
        payload.fa_stab = fractal_stability((const char (*)[FA10_LENGTH + 1])fa_history, window_len, 5);
        if (rt->fractal.raw_json) {
            payload.fa_map = strdup(rt->fractal.raw_json);
        } else {
            payload.fa_map = strdup("{}");
        }
        payload.r = rt->fractal.r;
        strncpy(payload.run_id, rt->config.run_id, sizeof(payload.run_id) - 1);
        strncpy(payload.cfg_hash, cfg_hash, sizeof(payload.cfg_hash) - 1);
        payload.eff_train = eff_train;
        payload.eff_val = eff_val;
        payload.eff_test = eff_test;
        snprintf(payload.explain, sizeof(payload.explain), "beam=%u lambda=%.3f", beam, lambda_override);
        if (rt->config.hmac_key[0]) {
            strcpy(payload.hmac_alg, "HMAC_SHA256");
        } else {
            payload.hmac_alg[0] = '\0';
        }
        strncpy(payload.salt, rt->config.salt, sizeof(payload.salt) - 1);

        char hash_hex[65];
        kolibri_hash_payload(&payload, hash_hex);
        char hmac_hex[65];
        kolibri_hmac_payload(&payload, rt->config.hmac_key, hmac_hex);
        kolibri_block_t block;
        block.payload = payload;
        strncpy(block.hash, hash_hex, sizeof(block.hash) - 1);
        if (rt->config.hmac_key[0]) {
            strncpy(block.hmac, hmac_hex, sizeof(block.hmac) - 1);
        } else {
            block.hmac[0] = '\0';
        }

        if (fmt_write_block(fp, &block.payload, block.hash, block.hmac) != 0) {
            free(payload.fa_map);
            fclose(fp);
            if (!out_chain) {
                kolibri_chain_free(&local_chain);
            }
            return -1;
        }
        fflush(fp);

        if (kolibri_chain_push(target_chain, &block) != 0) {
            free(payload.fa_map);
            fclose(fp);
            if (!out_chain) {
                kolibri_chain_free(&local_chain);
            }
            return -1;
        }

        free(block.payload.fa_map);
        block.payload.fa_map = NULL;

        strncpy(prev_hash, block.hash, sizeof(prev_hash) - 1);

        char event_buf[256];
        char num_buf[32];
        fmt_print_double_17g(num_buf, sizeof(num_buf), eff);
        snprintf(event_buf, sizeof(event_buf),
                 "{\"step\":%u,\"hash\":\"%s\",\"eff\":%s,\"compl\":%.3f,\"fa\":\"%s\"}",
                 step, block.hash, num_buf, compl, payload.fa);
        broadcast_event(cb, cb_data, "block", event_buf);

        struct timespec ts;
#if defined(CLOCK_MONOTONIC)
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double ms = ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#else
        clock_gettime(CLOCK_REALTIME, &ts);
        double ms = ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
        fmt_print_double_17g(num_buf, sizeof(num_buf), eff_val);
        snprintf(event_buf, sizeof(event_buf),
                 "{\"eff_val\":%s,\"compl\":%.3f,\"time_ms\":%.3f}",
                 num_buf, compl, ms);
        broadcast_event(cb, cb_data, "metric", event_buf);
    }

    fclose(fp);
    int verify_rc = kolibri_verify_file(log_path, &rt->config, 0);
    if (verify_rc == 0) {
        char verify_buf[64];
        snprintf(verify_buf, sizeof(verify_buf), "{\"ok\":true,\"blocks\":%u}", steps);
        broadcast_event(cb, cb_data, "verify", verify_buf);
    } else {
        broadcast_event(cb, cb_data, "verify", "{\"ok\":false,\"reason\":\"verification failed\"}");
    }
    if (!out_chain) {
        kolibri_chain_free(&local_chain);
    }
    return 0;
}
