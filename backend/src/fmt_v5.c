#include "fmt_v5.h"

#include "core.h"

#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t append(char *buffer, size_t buffer_len, size_t offset, const char *fmt, ...) {
    if (offset >= buffer_len) {
        return offset;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + offset, buffer_len - offset, fmt, args);
    va_end(args);
    if (written < 0) {
        return offset;
    }
    size_t w = (size_t)written;
    if (offset + w >= buffer_len) {
        return buffer_len;
    }
    return offset + w;
}

static size_t append_escaped(char *buffer, size_t buffer_len, size_t offset, const char *str) {
    offset = append(buffer, buffer_len, offset, "\"");
    for (const char *p = str; *p; ++p) {
        char c = *p;
        switch (c) {
            case '\\':
                offset = append(buffer, buffer_len, offset, "\\\\");
                break;
            case '"':
                offset = append(buffer, buffer_len, offset, "\\\"");
                break;
            case '\n':
                offset = append(buffer, buffer_len, offset, "\\n");
                break;
            case '\r':
                offset = append(buffer, buffer_len, offset, "\\r");
                break;
            case '\t':
                offset = append(buffer, buffer_len, offset, "\\t");
                break;
            default:
                offset = append(buffer, buffer_len, offset, "%c", c);
                break;
        }
    }
    offset = append(buffer, buffer_len, offset, "\"");
    return offset;
}

void fmt_init_locale(void) {
    setlocale(LC_ALL, "C");
}

void fmt_print_double_17g(char *buf, size_t len, double value) {
    if (!buf || len == 0) {
        return;
    }
    snprintf(buf, len, "%.17g", value);
}

size_t fmt_payload_json(const kolibri_payload_t *payload, char *buffer, size_t buffer_len) {
    if (!payload || !buffer || buffer_len == 0) {
        return 0;
    }
    size_t offset = 0;
    char num_buf[64];
    offset = append(buffer, buffer_len, offset, "{");
    offset = append(buffer, buffer_len, offset, "\"step\":%u", payload->step);
    offset = append(buffer, buffer_len, offset, ",\"parent\":%d", payload->parent);
    offset = append(buffer, buffer_len, offset, ",\"seed\":%llu", (unsigned long long)payload->seed);
    offset = append(buffer, buffer_len, offset, ",\"formula\":");
    offset = append_escaped(buffer, buffer_len, offset, payload->formula.repr);
    fmt_print_double_17g(num_buf, sizeof(num_buf), payload->eff);
    offset = append(buffer, buffer_len, offset, ",\"eff\":%s", num_buf);
    fmt_print_double_17g(num_buf, sizeof(num_buf), payload->compl);
    offset = append(buffer, buffer_len, offset, ",\"compl\":%s", num_buf);
    offset = append(buffer, buffer_len, offset, ",\"prev\":");
    offset = append_escaped(buffer, buffer_len, offset, payload->prev);
    offset = append(buffer, buffer_len, offset, ",\"votes\":[");
    for (size_t i = 0; i < FA10_LENGTH; ++i) {
        fmt_print_double_17g(num_buf, sizeof(num_buf), payload->votes[i]);
        offset = append(buffer, buffer_len, offset, "%s%s", i == 0 ? "" : ",", num_buf);
    }
    offset = append(buffer, buffer_len, offset, "]");
    offset = append(buffer, buffer_len, offset, ",\"fmt\":%u", payload->fmt);
    offset = append(buffer, buffer_len, offset, ",\"fa\":");
    offset = append_escaped(buffer, buffer_len, offset, payload->fa);
    offset = append(buffer, buffer_len, offset, ",\"fa_stab\":%zu", payload->fa_stab);
    offset = append(buffer, buffer_len, offset, ",\"fa_map\":");
    if (payload->fa_map && payload->fa_map[0] == '{') {
        offset = append(buffer, buffer_len, offset, "%s", payload->fa_map);
    } else {
        offset = append(buffer, buffer_len, offset, "{}");
    }
    fmt_print_double_17g(num_buf, sizeof(num_buf), payload->r);
    offset = append(buffer, buffer_len, offset, ",\"r\":%s", num_buf);
    offset = append(buffer, buffer_len, offset, ",\"run_id\":");
    offset = append_escaped(buffer, buffer_len, offset, payload->run_id);
    offset = append(buffer, buffer_len, offset, ",\"cfg_hash\":");
    offset = append_escaped(buffer, buffer_len, offset, payload->cfg_hash);
    fmt_print_double_17g(num_buf, sizeof(num_buf), payload->eff_train);
    offset = append(buffer, buffer_len, offset, ",\"eff_train\":%s", num_buf);
    fmt_print_double_17g(num_buf, sizeof(num_buf), payload->eff_val);
    offset = append(buffer, buffer_len, offset, ",\"eff_val\":%s", num_buf);
    fmt_print_double_17g(num_buf, sizeof(num_buf), payload->eff_test);
    offset = append(buffer, buffer_len, offset, ",\"eff_test\":%s", num_buf);
    offset = append(buffer, buffer_len, offset, ",\"explain\":");
    offset = append_escaped(buffer, buffer_len, offset, payload->explain);
    offset = append(buffer, buffer_len, offset, ",\"hmac_alg\":");
    offset = append_escaped(buffer, buffer_len, offset, payload->hmac_alg);
    offset = append(buffer, buffer_len, offset, ",\"salt\":");
    offset = append_escaped(buffer, buffer_len, offset, payload->salt);
    offset = append(buffer, buffer_len, offset, "}");
    return offset;
}

int fmt_write_block(FILE *fp, const kolibri_payload_t *payload, const char *hash_hex, const char *hmac_hex) {
    if (!fp || !payload || !hash_hex || !hmac_hex) {
        return -1;
    }
    char *payload_json = (char *)malloc(4096);
    if (!payload_json) {
        return -1;
    }
    size_t len = fmt_payload_json(payload, payload_json, 4096);
    int rc = fprintf(fp, "{\"payload\":%.*s,\"hash\":\"%s\",\"hmac\":\"%s\"}\n", (int)len, payload_json, hash_hex, hmac_hex);
    free(payload_json);
    if (rc < 0) {
        return -1;
    }
    return 0;
}
