#include "fractal.h"
#define JSMN_STATIC
#include "jsmn.h"
#include <math.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int token_equals(const char *json, const jsmntok_t *tok, const char *s) {
    size_t len = (size_t)(tok->end - tok->start);
    return strlen(s) == len && strncmp(json + tok->start, s, len) == 0;
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

static double parse_double(const char *json, const jsmntok_t *tok) {
    size_t len = (size_t)(tok->end - tok->start);
    char buf[64];
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, json + tok->start, len);
    buf[len] = '\0';
    return strtod(buf, NULL);
}

static int parse_int(const char *json, const jsmntok_t *tok) {
    return (int)parse_double(json, tok);
}

static void transform_init(FractalTransform *transform) {
    transform->op_count = 0;
    for (size_t i = 0; i < KOLIBRI_FA_MAX_OPS; i++) {
        transform->ops[i].type = FRACTAL_OP_NONE;
        transform->ops[i].value = 0.0;
        transform->ops[i].param_index = 0;
    }
}

static void add_op(FractalTransform *transform, FractalOpType type, double value, int param_index) {
    if (transform->op_count >= KOLIBRI_FA_MAX_OPS) {
        return;
    }
    transform->ops[transform->op_count].type = type;
    transform->ops[transform->op_count].value = value;
    transform->ops[transform->op_count].param_index = param_index;
    transform->op_count++;
}

static void parse_segment(const char *segment, FractalTransform *transform) {
    if (segment[0] == '\0') {
        return;
    }
    char buf[64];
    size_t len = strlen(segment);
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, segment, len);
    buf[len] = '\0';

    char *colon = strchr(buf, ':');
    if (colon == NULL) {
        if (strcmp(buf, "sin") == 0) {
            add_op(transform, FRACTAL_OP_APPLY_SIN, 0.0, 0);
        } else if (strcmp(buf, "tanh") == 0) {
            add_op(transform, FRACTAL_OP_APPLY_TANH, 0.0, 0);
        } else if (strcmp(buf, "exp") == 0) {
            add_op(transform, FRACTAL_OP_APPLY_EXP, 0.0, 0);
        } else if (strcmp(buf, "log") == 0) {
            add_op(transform, FRACTAL_OP_APPLY_LOG, 0.0, 0);
        }
        return;
    }
    *colon = '\0';
    const char *type = buf;
    const char *value_str = colon + 1;
    if (strcmp(type, "mul") == 0) {
        add_op(transform, FRACTAL_OP_MUL_CONST, strtod(value_str, NULL), 0);
    } else if (strcmp(type, "add") == 0) {
        add_op(transform, FRACTAL_OP_ADD_CONST, strtod(value_str, NULL), 0);
    } else if (strcmp(type, "param") == 0) {
        add_op(transform, FRACTAL_OP_ADD_PARAM, 0.0, atoi(value_str));
    } else if (strcmp(type, "sub_param") == 0) {
        add_op(transform, FRACTAL_OP_SUB_PARAM, 0.0, atoi(value_str));
    } else if (strcmp(type, "pow_param") == 0) {
        add_op(transform, FRACTAL_OP_POW_PARAM, 0.0, atoi(value_str));
    }
}

static void parse_operations(const char *json, const jsmntok_t *tok, FractalTransform *transform) {
    transform_init(transform);
    size_t len = (size_t)(tok->end - tok->start);
    char *buf = (char *)malloc(len + 1);
    if (buf == NULL) {
        return;
    }
    memcpy(buf, json + tok->start, len);
    buf[len] = '\0';
    char *cursor = buf;
    while (cursor != NULL && *cursor != '\0') {
        char *next = strchr(cursor, '|');
        if (next != NULL) {
            *next = '\0';
        }
        parse_segment(cursor, transform);
        if (next == NULL) {
            break;
        }
        cursor = next + 1;
    }
    free(buf);
}

int fractal_load_map(const char *path, FractalMap *map, char *err, size_t err_size) {
    if (map == NULL) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "map pointer null");
        }
        return -1;
    }
    map->r = 0.5;
    map->level_count = 0;
    map->stab_window = 5;
    for (size_t i = 0; i < KOLIBRI_FA_LEVELS; i++) {
        for (size_t j = 0; j < KOLIBRI_FA_DIGITS; j++) {
            transform_init(&map->transforms[i][j]);
        }
    }
    char *json = NULL;
    size_t len = 0;
    if (read_file(path, &json, &len) != 0) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "cannot read %s", path);
        }
        return -1;
    }

    jsmn_parser parser;
    jsmn_init(&parser);
    const unsigned int max_tokens = 1024;
    jsmntok_t *tokens = (jsmntok_t *)calloc(max_tokens, sizeof(jsmntok_t));
    if (tokens == NULL) {
        free(json);
        if (err && err_size > 0) {
            snprintf(err, err_size, "alloc tokens");
        }
        return -1;
    }
    int parsed = jsmn_parse(&parser, json, len, tokens, max_tokens);
    if (parsed < 0) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "parse error %d", parsed);
        }
        free(tokens);
        free(json);
        return -1;
    }
    if (tokens[0].type != JSMN_OBJECT) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "root not object");
        }
        free(tokens);
        free(json);
        return -1;
    }

    int idx = 1;
    for (int i = 0; i < tokens[0].size; i++) {
        jsmntok_t *key = &tokens[idx++];
        jsmntok_t *val = &tokens[idx];
        if (token_equals(json, key, "r")) {
            map->r = parse_double(json, val);
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "window")) {
            int w = parse_int(json, val);
            if (w > 0) {
                map->stab_window = (size_t)w;
            }
            idx = skip_token(tokens, idx);
        } else if (token_equals(json, key, "levels")) {
            if (val->type != JSMN_ARRAY) {
                if (err && err_size > 0) {
                    snprintf(err, err_size, "levels not array");
                }
                free(tokens);
                free(json);
                return -1;
            }
            int level_idx = idx + 1;
            int levels = val->size;
            if (levels > KOLIBRI_FA_LEVELS) {
                levels = KOLIBRI_FA_LEVELS;
            }
            map->level_count = (size_t)levels;
            for (int l = 0; l < val->size; l++) {
                jsmntok_t *level_tok = &tokens[level_idx];
                if (level_tok->type != JSMN_ARRAY) {
                    if (err && err_size > 0) {
                        snprintf(err, err_size, "level %d not array", l);
                    }
                    free(tokens);
                    free(json);
                    return -1;
                }
                int digit_count = level_tok->size;
                int entry_idx = level_idx + 1;
                for (int d = 0; d < digit_count; d++) {
                    jsmntok_t *entry = &tokens[entry_idx];
                    if (entry->type == JSMN_STRING) {
                        if (l < KOLIBRI_FA_LEVELS && d < KOLIBRI_FA_DIGITS) {
                            parse_operations(json, entry, &map->transforms[l][d]);
                        }
                    }
                    entry_idx = skip_token(tokens, entry_idx);
                }
                level_idx = skip_token(tokens, level_idx);
            }
            idx = skip_token(tokens, idx);
        } else {
            idx = skip_token(tokens, idx);
        }
    }

    free(tokens);
    free(json);
    if (map->level_count == 0) {
        map->level_count = KOLIBRI_FA_LEVELS;
    }
    return 0;
}

void fractal_generate_votes(uint64_t seed, double votes[KOLIBRI_FA_DIGITS]) {
    uint64_t state = seed == 0 ? 0x6a09e667f3bcc908ULL : seed;
    for (size_t i = 0; i < KOLIBRI_FA_DIGITS; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        uint64_t mantissa = state >> 11;
        double value = (double)mantissa / 9007199254740992.0;
        if (value < 0.0) {
            value = 0.0;
        }
        if (value > 1.0) {
            value = 1.0;
        }
        votes[i] = value;
    }
}

void fractal_votes_to_address(const double votes[KOLIBRI_FA_DIGITS], char fa_out[KOLIBRI_FA_DIGITS + 1]) {
    for (size_t i = 0; i < KOLIBRI_FA_DIGITS; i++) {
        double v = votes[i];
        if (v < 0.0) {
            v = 0.0;
        }
        if (v > 1.0) {
            v = 1.0;
        }
        int digit = (int)llround(9.0 * v);
        if (digit < 0) {
            digit = 0;
        }
        if (digit > 9) {
            digit = 9;
        }
        fa_out[i] = (char)('0' + digit);
    }
    fa_out[KOLIBRI_FA_DIGITS] = '\0';
}

size_t fractal_stability(const char window[][KOLIBRI_FA_DIGITS + 1], size_t count) {
    if (count == 0) {
        return 0;
    }
    size_t max_len = KOLIBRI_FA_DIGITS;
    size_t prefix = 0;
    for (size_t pos = 0; pos < max_len; pos++) {
        char c = window[0][pos];
        for (size_t i = 1; i < count; i++) {
            if (window[i][pos] != c) {
                return prefix;
            }
        }
        prefix++;
    }
    return prefix;
}

static DSLNode *apply_op(DSLNode *node, const FractalOp *op) {
    DSLNode *new_node = NULL;
    switch (op->type) {
        case FRACTAL_OP_NONE:
            return node;
        case FRACTAL_OP_ADD_CONST: {
            DSLNode *rhs = dsl_node_const(op->value);
            new_node = dsl_node_binary(DSL_NODE_ADD, node, rhs);
            break;
        }
        case FRACTAL_OP_MUL_CONST: {
            DSLNode *lhs = dsl_node_const(op->value);
            new_node = dsl_node_binary(DSL_NODE_MUL, lhs, node);
            break;
        }
        case FRACTAL_OP_ADD_PARAM: {
            DSLNode *rhs = dsl_node_param(op->param_index);
            new_node = dsl_node_binary(DSL_NODE_ADD, node, rhs);
            break;
        }
        case FRACTAL_OP_SUB_PARAM: {
            DSLNode *rhs = dsl_node_param(op->param_index);
            new_node = dsl_node_binary(DSL_NODE_SUB, node, rhs);
            break;
        }
        case FRACTAL_OP_APPLY_SIN:
            new_node = dsl_node_unary(DSL_NODE_SIN, node);
            break;
        case FRACTAL_OP_APPLY_TANH:
            new_node = dsl_node_unary(DSL_NODE_TANH, node);
            break;
        case FRACTAL_OP_APPLY_EXP:
            new_node = dsl_node_unary(DSL_NODE_EXP, node);
            break;
        case FRACTAL_OP_APPLY_LOG:
            new_node = dsl_node_unary(DSL_NODE_LOG, node);
            break;
        case FRACTAL_OP_POW_PARAM: {
            DSLNode *rhs = dsl_node_param(op->param_index);
            new_node = dsl_node_binary(DSL_NODE_POW, node, rhs);
            break;
        }
    }
    if (new_node == NULL) {
        return node;
    }
    return new_node;
}

DSLNode *fractal_build_formula(const FractalMap *map, const char *fa, const double votes[KOLIBRI_FA_DIGITS]) {
    if (map == NULL || fa == NULL) {
        return NULL;
    }
    DSLNode *node = dsl_node_binary(
        DSL_NODE_ADD,
        dsl_node_binary(DSL_NODE_MUL, dsl_node_var_x(), dsl_node_param(1)),
        dsl_node_param(0));
    if (node == NULL) {
        return NULL;
    }
    double scale_value = 0.8 + votes[0] * 0.4;
    DSLNode *scale = dsl_node_const(scale_value);
    node = dsl_node_binary(DSL_NODE_MUL, scale, node);
    size_t levels = map->level_count;
    if (levels > KOLIBRI_FA_LEVELS) {
        levels = KOLIBRI_FA_LEVELS;
    }
    for (size_t i = 0; i < levels; i++) {
        int digit = fa[i] >= '0' && fa[i] <= '9' ? fa[i] - '0' : 0;
        FractalTransform transform = map->transforms[i][digit];
        for (size_t op_index = 0; op_index < transform.op_count; op_index++) {
            node = apply_op(node, &transform.ops[op_index]);
        }
    }
    DSLNode *vote_term = dsl_node_const(map->r * votes[9]);
    node = dsl_node_binary(DSL_NODE_ADD, node, vote_term);
    return node;
}
