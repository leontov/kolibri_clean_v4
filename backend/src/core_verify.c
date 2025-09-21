#include "core.h"
#include "fmt_v5.h"
#define JSMN_STATIC
#include "jsmn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int extract_votes(const char *json, const jsmntok_t *tokens, int idx, double votes[KOLIBRI_VOTE_COUNT]) {
    const jsmntok_t *array_tok = &tokens[idx];
    if (array_tok->type != JSMN_ARRAY) {
        return -1;
    }
    if (array_tok->size != KOLIBRI_VOTE_COUNT) {
        return -1;
    }
    int cur = idx + 1;
    for (int i = 0; i < KOLIBRI_VOTE_COUNT; i++) {
        const jsmntok_t *value_tok = &tokens[cur];
        votes[i] = parse_token_double(json, value_tok);
        cur = skip_token(tokens, cur);
    }
    return cur;
}

static int parse_block(const char *json, ReasonBlock *block) {
    jsmn_parser parser;
    jsmn_init(&parser);
    const unsigned int max_tokens = 512;
    jsmntok_t *tokens = (jsmntok_t *)calloc(max_tokens, sizeof(jsmntok_t));
    if (tokens == NULL) {
        return -1;
    }
    int parsed = jsmn_parse(&parser, json, strlen(json), tokens, max_tokens);
    if (parsed < 0 || tokens[0].type != JSMN_OBJECT) {
        free(tokens);
        return -1;
    }

    memset(block, 0, sizeof(*block));
    int idx = 1;
    for (int i = 0; i < tokens[0].size; i++) {
        jsmntok_t *key = &tokens[idx++];
        jsmntok_t *val = &tokens[idx];
        if (token_equals(json, key, "step")) {
            block->step = (int)parse_token_int(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "parent")) {
            block->parent = (int)parse_token_int(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "seed")) {
            block->seed = parse_token_uint64(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "formula")) {
            copy_token_string(json, val, block->formula, sizeof(block->formula));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "eff")) {
            block->eff = parse_token_double(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "compl")) {
            block->compl = parse_token_double(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "prev")) {
            copy_token_string(json, val, block->prev, sizeof(block->prev));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "votes")) {
            int next = extract_votes(json, tokens, idx, block->votes);
            if (next < 0) {
                free(tokens);
                return -1;
            }
            idx = next;
        } else if (token_equals(json, key, "fmt")) {
            block->fmt = (int)parse_token_int(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "fa")) {
            copy_token_string(json, val, block->fa, sizeof(block->fa));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "fa_stab")) {
            block->fa_stab = (int)parse_token_int(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "fa_map")) {
            copy_token_string(json, val, block->fa_map, sizeof(block->fa_map));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "r")) {
            block->r = parse_token_double(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "run_id")) {
            copy_token_string(json, val, block->run_id, sizeof(block->run_id));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "cfg_hash")) {
            copy_token_string(json, val, block->cfg_hash, sizeof(block->cfg_hash));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "eff_train")) {
            block->eff_train = parse_token_double(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "eff_val")) {
            block->eff_val = parse_token_double(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "eff_test")) {
            block->eff_test = parse_token_double(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "explain")) {
            copy_token_string(json, val, block->explain, sizeof(block->explain));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "hmac_alg")) {
            copy_token_string(json, val, block->hmac_alg, sizeof(block->hmac_alg));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "salt")) {
            copy_token_string(json, val, block->salt, sizeof(block->salt));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "hash")) {
            copy_token_string(json, val, block->hash_hex, sizeof(block->hash_hex));
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "hmac")) {
            copy_token_string(json, val, block->hmac_hex, sizeof(block->hmac_hex));
            idx = skip_token(tokens, idx);
        } else {
            idx = skip_token(tokens, idx);
        }
    }
    free(tokens);
    return 0;
}

int core_verify_chain(const char *path,
                      const char *hmac_key,
                      size_t *out_blocks,
                      size_t *failed_step,
                      char *err,
                      size_t err_size) {
    if (out_blocks) {
        *out_blocks = 0;
    }
    if (failed_step) {
        *failed_step = 0;
    }
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "cannot open %s", path);
        }
        return -1;
    }
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    char prev_hash[KOLIBRI_HASH_TEXT];
    prev_hash[0] = '\0';
    size_t count = 0;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            line[--linelen] = '\0';
        }
        ReasonBlock block;
        if (parse_block(line, &block) != 0) {
            if (err && err_size > 0) {
                snprintf(err, err_size, "parse error at line %zu", count + 1);
            }
            if (failed_step) {
                *failed_step = count + 1;
            }
            free(line);
            fclose(fp);
            return -1;
        }

        char canonical[4096];
        size_t canonical_len = 0;
        if (fmt_build_json(&block, 1, block.hash_hex, block.hmac_hex, canonical, sizeof(canonical), &canonical_len) != 0) {
            if (err && err_size > 0) {
                snprintf(err, err_size, "serialization failed");
            }
            if (failed_step) {
                *failed_step = block.step;
            }
            free(line);
            fclose(fp);
            return -1;
        }
        if (strcmp(canonical, line) != 0) {
            if (err && err_size > 0) {
                snprintf(err, err_size, "non-canonical line %zu", count + 1);
            }
            if (failed_step) {
                *failed_step = block.step;
            }
            free(line);
            fclose(fp);
            return -1;
        }

        char payload[4096];
        size_t payload_len = 0;
        if (fmt_build_json(&block, 0, NULL, NULL, payload, sizeof(payload), &payload_len) != 0) {
            if (err && err_size > 0) {
                snprintf(err, err_size, "payload build failed");
            }
            if (failed_step) {
                *failed_step = block.step;
            }
            free(line);
            fclose(fp);
            return -1;
        }

        char expected_hash[KOLIBRI_HASH_TEXT];
        core_compute_sha256_hex((unsigned char *)payload, payload_len, expected_hash);
        if (strcmp(expected_hash, block.hash_hex) != 0) {
            if (err && err_size > 0) {
                snprintf(err, err_size, "hash mismatch at step %d", block.step);
            }
            if (failed_step) {
                *failed_step = block.step;
            }
            free(line);
            fclose(fp);
            return -1;
        }

        if (block.hmac_alg[0] != '\0') {
            if (hmac_key == NULL || hmac_key[0] == '\0') {
                if (err && err_size > 0) {
                    snprintf(err, err_size, "missing HMAC key at step %d", block.step);
                }
                if (failed_step) {
                    *failed_step = block.step;
                }
                free(line);
                fclose(fp);
                return -1;
            }
            char expected_hmac[KOLIBRI_HASH_TEXT];
            core_compute_hmac_sha256_hex((unsigned char *)payload, payload_len, hmac_key, expected_hmac);
            if (strcmp(expected_hmac, block.hmac_hex) != 0) {
                if (err && err_size > 0) {
                    snprintf(err, err_size, "hmac mismatch at step %d", block.step);
                }
                if (failed_step) {
                    *failed_step = block.step;
                }
                free(line);
                fclose(fp);
                return -1;
            }
        } else {
            if (hmac_key != NULL && hmac_key[0] != '\0') {
                if (err && err_size > 0) {
                    snprintf(err, err_size, "log missing hmac");
                }
                if (failed_step) {
                    *failed_step = block.step;
                }
                free(line);
                fclose(fp);
                return -1;
            }
            if (block.hmac_hex[0] != '\0') {
                if (err && err_size > 0) {
                    snprintf(err, err_size, "unexpected hmac at step %d", block.step);
                }
                if (failed_step) {
                    *failed_step = block.step;
                }
                free(line);
                fclose(fp);
                return -1;
            }
        }

        if (count == 0) {
            if (block.prev[0] != '\0') {
                if (err && err_size > 0) {
                    snprintf(err, err_size, "prev mismatch at step %d", block.step);
                }
                if (failed_step) {
                    *failed_step = block.step;
                }
                free(line);
                fclose(fp);
                return -1;
            }
        } else {
            if (strcmp(block.prev, prev_hash) != 0) {
                if (err && err_size > 0) {
                    snprintf(err, err_size, "prev link mismatch at step %d", block.step);
                }
                if (failed_step) {
                    *failed_step = block.step;
                }
                free(line);
                fclose(fp);
                return -1;
            }
        }

        strncpy(prev_hash, block.hash_hex, sizeof(prev_hash) - 1);
        prev_hash[sizeof(prev_hash) - 1] = '\0';
        count++;
    }
    free(line);
    fclose(fp);
    if (out_blocks) {
        *out_blocks = count;
    }
    return 0;
}
