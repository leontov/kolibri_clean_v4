#include "core.h"

#include "fmt_v5.h"

#include <errno.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_json_string(const char *json, const char *key, char *out, size_t out_len) {
    const char *pos = strstr(json, key);
    if (!pos) {
        return -1;
    }
    pos = strchr(pos + strlen(key), ':');
    if (!pos) {
        return -1;
    }
    ++pos;
    while (*pos && *pos != '"') {
        ++pos;
    }
    if (*pos != '"') {
        return -1;
    }
    ++pos;
    const char *start = pos;
    while (*pos && *pos != '"') {
        if (*pos == '\\' && *(pos + 1)) {
            pos += 2;
        } else {
            ++pos;
        }
    }
    const char *end = pos;
    if (!end) {
        return -1;
    }
    size_t len = (size_t)(end - start);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static int parse_json_number(const char *json, const char *key, double *out_double, long long *out_ll) {
    const char *pos = strstr(json, key);
    if (!pos) {
        return -1;
    }
    pos = strchr(pos + strlen(key), ':');
    if (!pos) {
        return -1;
    }
    ++pos;
    if (out_double) {
        *out_double = strtod(pos, NULL);
    }
    if (out_ll) {
        *out_ll = strtoll(pos, NULL, 10);
    }
    return 0;
}

int kolibri_load_config(const char *path, kolibri_config_t *cfg) {
    if (!cfg || !path) {
        return -1;
    }
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->cfg_path, path, sizeof(cfg->cfg_path) - 1);
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return -1;
    }
    rewind(fp);
    char *buffer = (char *)malloc((size_t)len + 1);
    if (!buffer) {
        fclose(fp);
        return -1;
    }
    size_t read = fread(buffer, 1, (size_t)len, fp);
    fclose(fp);
    buffer[read] = '\0';
    if (parse_json_string(buffer, "\"run_id\"", cfg->run_id, sizeof(cfg->run_id)) != 0) {
        strcpy(cfg->run_id, "kolibri-demo");
    }
    if (parse_json_string(buffer, "\"fractal_map\"", cfg->fractal_map_path, sizeof(cfg->fractal_map_path)) != 0) {
        strcpy(cfg->fractal_map_path, "configs/fractal_map.default.json");
    }
    if (parse_json_string(buffer, "\"dataset\"", cfg->dataset_path, sizeof(cfg->dataset_path)) != 0) {
        strcpy(cfg->dataset_path, "datasets/demo.csv");
    }
    if (parse_json_string(buffer, "\"salt\"", cfg->salt, sizeof(cfg->salt)) != 0) {
        strcpy(cfg->salt, "kolibri");
    }
    if (parse_json_string(buffer, "\"hmac_key\"", cfg->hmac_key, sizeof(cfg->hmac_key)) != 0) {
        cfg->hmac_key[0] = '\0';
    }
    double fmt_val = 5;
    parse_json_number(buffer, "\"fmt\"", &fmt_val, NULL);
    cfg->fmt_default = (unsigned)fmt_val;
    double lambda_val = 0.01;
    parse_json_number(buffer, "\"lambda\"", &lambda_val, NULL);
    cfg->lambda_default = lambda_val;
    long long seed_val = 42;
    parse_json_number(buffer, "\"seed\"", NULL, &seed_val);
    cfg->seed_default = (uint64_t)seed_val;
    free(buffer);
    return 0;
}

int dataset_load(const char *path, dataset_t *dataset) {
    if (!dataset || !path) {
        return -1;
    }
    memset(dataset, 0, sizeof(*dataset));
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    size_t capacity = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#') {
            continue;
        }
        double x, y;
        if (sscanf(line, "%lf,%lf", &x, &y) == 2) {
            if (dataset->length == capacity) {
                capacity = capacity ? capacity * 2 : 32;
                dataset_point_t *tmp = (dataset_point_t *)realloc(dataset->points, capacity * sizeof(dataset_point_t));
                if (!tmp) {
                    fclose(fp);
                    return -1;
                }
                dataset->points = tmp;
            }
            dataset->points[dataset->length].x = x;
            dataset->points[dataset->length].y = y;
            dataset->length += 1;
        }
    }
    fclose(fp);
    return dataset->length > 0 ? 0 : -1;
}

void dataset_free(dataset_t *dataset) {
    if (!dataset) {
        return;
    }
    free(dataset->points);
    dataset->points = NULL;
    dataset->length = 0;
}

int kolibri_runtime_init(kolibri_runtime_t *rt, const char *cfg_path) {
    if (!rt) {
        return -1;
    }
    memset(rt, 0, sizeof(*rt));
    if (kolibri_load_config(cfg_path, &rt->config) != 0) {
        return -1;
    }
    if (fractal_load_map(rt->config.fractal_map_path, &rt->fractal) != 0) {
        return -1;
    }
    if (dataset_load(rt->config.dataset_path, &rt->dataset) != 0) {
        fractal_free_map(&rt->fractal);
        return -1;
    }
    return 0;
}

void kolibri_runtime_free(kolibri_runtime_t *rt) {
    if (!rt) {
        return;
    }
    dataset_free(&rt->dataset);
    fractal_free_map(&rt->fractal);
}

void kolibri_chain_init(kolibri_chain_t *chain) {
    if (!chain) {
        return;
    }
    chain->blocks = NULL;
    chain->count = 0;
    chain->capacity = 0;
}

static void free_block(kolibri_block_t *block) {
    if (!block) {
        return;
    }
    free(block->payload.fa_map);
    block->payload.fa_map = NULL;
}

void kolibri_chain_free(kolibri_chain_t *chain) {
    if (!chain) {
        return;
    }
    for (size_t i = 0; i < chain->count; ++i) {
        free_block(&chain->blocks[i]);
    }
    free(chain->blocks);
    chain->blocks = NULL;
    chain->count = 0;
    chain->capacity = 0;
}

int kolibri_chain_push(kolibri_chain_t *chain, const kolibri_block_t *block) {
    if (!chain || !block) {
        return -1;
    }
    if (chain->count == chain->capacity) {
        size_t new_cap = chain->capacity ? chain->capacity * 2 : 16;
        kolibri_block_t *tmp = (kolibri_block_t *)realloc(chain->blocks, new_cap * sizeof(kolibri_block_t));
        if (!tmp) {
            return -1;
        }
        chain->blocks = tmp;
        chain->capacity = new_cap;
    }
    chain->blocks[chain->count] = *block;
    if (block->payload.fa_map) {
        chain->blocks[chain->count].payload.fa_map = strdup(block->payload.fa_map);
    }
    chain->count += 1;
    return 0;
}

int kolibri_chain_tail_copy(const kolibri_chain_t *chain, size_t tail, kolibri_chain_t *out) {
    if (!chain || !out) {
        return -1;
    }
    kolibri_chain_init(out);
    if (tail == 0 || chain->count == 0) {
        return 0;
    }
    size_t start = chain->count > tail ? chain->count - tail : 0;
    for (size_t i = start; i < chain->count; ++i) {
        if (kolibri_chain_push(out, &chain->blocks[i]) != 0) {
            kolibri_chain_free(out);
            return -1;
        }
    }
    return 0;
}

static void to_hex(const unsigned char *data, size_t len, char out_hex[65]) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out_hex[i * 2] = hex[(data[i] >> 4) & 0xF];
        out_hex[i * 2 + 1] = hex[data[i] & 0xF];
    }
    out_hex[len * 2] = '\0';
}

void kolibri_hash_payload(const kolibri_payload_t *payload, char out_hex[65]) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    char buffer[4096];
    size_t len = fmt_payload_json(payload, buffer, sizeof(buffer));
    SHA256((const unsigned char *)buffer, len, digest);
    to_hex(digest, SHA256_DIGEST_LENGTH, out_hex);
}

void kolibri_hmac_payload(const kolibri_payload_t *payload, const char *key, char out_hex[65]) {
    if (!key || key[0] == '\0') {
        out_hex[0] = '\0';
        return;
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    char buffer[4096];
    size_t len = fmt_payload_json(payload, buffer, sizeof(buffer));
    HMAC(EVP_sha256(), key, (int)strlen(key), (const unsigned char *)buffer, len, digest, &digest_len);
    to_hex(digest, digest_len, out_hex);
}

static const char *parse_string_field(const char *p, char *out, size_t out_len) {
    if (*p != '"') {
        return NULL;
    }
    ++p;
    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) {
            p += 2;
            continue;
        }
        ++p;
    }
    if (*p != '"') {
        return NULL;
    }
    size_t len = (size_t)(p - start);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return p + 1;
}

static const char *parse_double_field(const char *p, double *out) {
    char *end = NULL;
    double v = strtod(p, &end);
    if (!end) {
        return NULL;
    }
    if (out) {
        *out = v;
    }
    return end;
}

static const char *parse_uint_field(const char *p, unsigned *out) {
    char *end = NULL;
    unsigned long v = strtoul(p, &end, 10);
    if (!end) {
        return NULL;
    }
    if (out) {
        *out = (unsigned)v;
    }
    return end;
}

static const char *parse_int_field(const char *p, int *out) {
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (!end) {
        return NULL;
    }
    if (out) {
        *out = (int)v;
    }
    return end;
}

static const char *parse_u64_field(const char *p, uint64_t *out) {
    char *end = NULL;
    unsigned long long v = strtoull(p, &end, 10);
    if (!end) {
        return NULL;
    }
    if (out) {
        *out = (uint64_t)v;
    }
    return end;
}

static const char *expect_marker(const char *p, const char *marker) {
    size_t marker_len = strlen(marker);
    if (strncmp(p, marker, marker_len) != 0) {
        return NULL;
    }
    return p + marker_len;
}

int kolibri_parse_block_line(const char *line, size_t len, kolibri_payload_t *payload, char *hash_hex, char *hmac_hex) {
    if (!line || len == 0 || !payload) {
        return -1;
    }
    const char *payload_start = strstr(line, "{\"payload\":");
    if (!payload_start) {
        return -1;
    }
    payload_start += strlen("{\"payload\":");
    const char *hash_pos = strstr(line, ",\"hash\":\"");
    if (!hash_pos) {
        return -1;
    }
    const char *payload_end = hash_pos;
    size_t payload_len = (size_t)(payload_end - payload_start);
    char *payload_json = (char *)malloc(payload_len + 1);
    if (!payload_json) {
        return -1;
    }
    memcpy(payload_json, payload_start, payload_len);
    payload_json[payload_len] = '\0';

    const char *hash_start = hash_pos + strlen(",\"hash\":\"");
    const char *hash_end = strchr(hash_start, '"');
    if (!hash_end) {
        free(payload_json);
        return -1;
    }
    if (hash_hex) {
        size_t hlen = (size_t)(hash_end - hash_start);
        memcpy(hash_hex, hash_start, hlen);
        hash_hex[hlen] = '\0';
    }
    const char *hmac_pos = strstr(hash_end, ",\"hmac\":\"");
    if (!hmac_pos) {
        free(payload_json);
        return -1;
    }
    const char *hmac_start = hmac_pos + strlen(",\"hmac\":\"");
    const char *hmac_end = strchr(hmac_start, '"');
    if (!hmac_end) {
        free(payload_json);
        return -1;
    }
    if (hmac_hex) {
        size_t hlen = (size_t)(hmac_end - hmac_start);
        memcpy(hmac_hex, hmac_start, hlen);
        hmac_hex[hlen] = '\0';
    }
    memset(payload, 0, sizeof(*payload));
    const char *p = payload_json;
    if (*p != '{') {
        free(payload_json);
        return -1;
    }
    ++p;
    if (!(p = expect_marker(p, "\"step\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_uint_field(p, &payload->step))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"parent\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_int_field(p, &payload->parent))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"seed\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_u64_field(p, &payload->seed))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"formula\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_string_field(p, payload->formula.repr, sizeof(payload->formula.repr)))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"eff\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_double_field(p, &payload->eff))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"compl\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_double_field(p, &payload->compl))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"prev\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_string_field(p, payload->prev, sizeof(payload->prev)))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"votes\":"))) {
        free(payload_json);
        return -1;
    }
    if (*p != '[') {
        free(payload_json);
        return -1;
    }
    ++p;
    for (size_t i = 0; i < FA10_LENGTH; ++i) {
        if (!(p = parse_double_field(p, &payload->votes[i]))) {
            free(payload_json);
            return -1;
        }
        if (i < FA10_LENGTH - 1) {
            if (*p != ',') {
                free(payload_json);
                return -1;
            }
            ++p;
        }
    }
    if (*p != ']') {
        free(payload_json);
        return -1;
    }
    ++p;
    if (!(p = expect_marker(p, ",\"fmt\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_uint_field(p, &payload->fmt))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"fa\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_string_field(p, payload->fa, sizeof(payload->fa)))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"fa_stab\":"))) {
        free(payload_json);
        return -1;
    }
    double stab = 0.0;
    if (!(p = parse_double_field(p, &stab))) {
        free(payload_json);
        return -1;
    }
    payload->fa_stab = (size_t)stab;
    if (!(p = expect_marker(p, ",\"fa_map\":"))) {
        free(payload_json);
        return -1;
    }
    if (*p == '{') {
        int depth = 0;
        const char *start = p;
        do {
            if (*p == '{') {
                depth++;
            } else if (*p == '}') {
                depth--;
            }
            ++p;
        } while (depth > 0 && *p);
        size_t mlen = (size_t)(p - start);
        payload->fa_map = (char *)malloc(mlen + 1);
        memcpy(payload->fa_map, start, mlen);
        payload->fa_map[mlen] = '\0';
    } else {
        payload->fa_map = strdup("{}");
    }
    if (!(p = expect_marker(p, ",\"r\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_double_field(p, &payload->r))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"run_id\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_string_field(p, payload->run_id, sizeof(payload->run_id)))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"cfg_hash\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_string_field(p, payload->cfg_hash, sizeof(payload->cfg_hash)))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"eff_train\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_double_field(p, &payload->eff_train))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"eff_val\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_double_field(p, &payload->eff_val))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"eff_test\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_double_field(p, &payload->eff_test))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"explain\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_string_field(p, payload->explain, sizeof(payload->explain)))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"hmac_alg\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_string_field(p, payload->hmac_alg, sizeof(payload->hmac_alg)))) {
        free(payload_json);
        return -1;
    }
    if (!(p = expect_marker(p, ",\"salt\":"))) {
        free(payload_json);
        return -1;
    }
    if (!(p = parse_string_field(p, payload->salt, sizeof(payload->salt)))) {
        free(payload_json);
        return -1;
    }
    free(payload_json);
    return 0;
}
