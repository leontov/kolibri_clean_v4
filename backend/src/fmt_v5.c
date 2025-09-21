#include "fmt_v5.h"
#include "core.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fmt_init_locale(void) {
    setlocale(LC_NUMERIC, "C");
}

typedef struct {
    char *buf;
    size_t size;
    size_t len;
    int first;
    int error;
} fmt_buffer;

static void buffer_init(fmt_buffer *buffer, char *out, size_t size) {
    buffer->buf = out;
    buffer->size = size;
    buffer->len = 0;
    buffer->first = 1;
    buffer->error = 0;
    if (size == 0) {
        return;
    }
    buffer->buf[0] = '\0';
    if (size > 1) {
        buffer->buf[0] = '{';
        buffer->buf[1] = '\0';
        buffer->len = 1;
    }
}

static int buffer_append(fmt_buffer *buffer, const char *data, size_t len) {
    if (buffer->error) {
        return -1;
    }
    if (buffer->size == 0) {
        buffer->error = 1;
        return -1;
    }
    if (buffer->len + len >= buffer->size) {
        buffer->error = 1;
        return -1;
    }
    memcpy(buffer->buf + buffer->len, data, len);
    buffer->len += len;
    buffer->buf[buffer->len] = '\0';
    return 0;
}

static int buffer_append_char(fmt_buffer *buffer, char c) {
    return buffer_append(buffer, &c, 1);
}

static int buffer_field_prefix(fmt_buffer *buffer) {
    if (buffer->error) {
        return -1;
    }
    if (buffer->first) {
        buffer->first = 0;
        return 0;
    }
    return buffer_append_char(buffer, ',');
}

static int buffer_append_raw(fmt_buffer *buffer, const char *s) {
    return buffer_append(buffer, s, strlen(s));
}

int fmt_escape_string(const char *input, char *output, size_t output_size) {
    size_t out_pos = 0;
    for (size_t i = 0; input[i] != '\0'; i++) {
        unsigned char c = (unsigned char)input[i];
        const char *escaped = NULL;
        char unicode[7];
        switch (c) {
            case '\"':
                escaped = "\\\"";
                break;
            case '\\':
                escaped = "\\\\";
                break;
            case '\b':
                escaped = "\\b";
                break;
            case '\f':
                escaped = "\\f";
                break;
            case '\n':
                escaped = "\\n";
                break;
            case '\r':
                escaped = "\\r";
                break;
            case '\t':
                escaped = "\\t";
                break;
            default:
                if (c < 0x20) {
                    snprintf(unicode, sizeof(unicode), "\\u%04x", c);
                    escaped = unicode;
                }
                break;
        }
        if (escaped != NULL) {
            size_t needed = strlen(escaped);
            if (out_pos + needed >= output_size) {
                return -1;
            }
            memcpy(output + out_pos, escaped, needed);
            out_pos += needed;
        } else {
            if (out_pos + 1 >= output_size) {
                return -1;
            }
            output[out_pos++] = (char)c;
        }
    }
    if (out_pos >= output_size) {
        return -1;
    }
    output[out_pos] = '\0';
    return (int)out_pos;
}

int fmt_print_double(char *buf, size_t buf_size, double value) {
    if (buf_size == 0) {
        return -1;
    }
    if (value == 0.0) {
        value = 0.0;
    }
    int written = snprintf(buf, buf_size, "%.17g", value);
    if (written < 0 || (size_t)written >= buf_size) {
        return -1;
    }
    if (strcmp(buf, "-0") == 0) {
        if (buf_size >= 2) {
            buf[0] = '0';
            buf[1] = '\0';
            written = 1;
        }
    }
    return written;
}

static int buffer_add_string(fmt_buffer *buffer, const char *name, const char *value) {
    if (value == NULL) {
        value = "";
    }
    if (buffer_field_prefix(buffer) != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    if (buffer_append(buffer, name, strlen(name)) != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, ':') != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    size_t len = strlen(value);
    size_t capacity = len * 6 + 1;
    char *escaped = (char *)malloc(capacity);
    if (escaped == NULL) {
        buffer->error = 1;
        return -1;
    }
    if (fmt_escape_string(value, escaped, capacity) < 0) {
        free(escaped);
        buffer->error = 1;
        return -1;
    }
    int res = buffer_append(buffer, escaped, strlen(escaped));
    free(escaped);
    if (res != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    return 0;
}

static int buffer_add_int(fmt_buffer *buffer, const char *name, long long value) {
    if (buffer_field_prefix(buffer) != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    if (buffer_append(buffer, name, strlen(name)) != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, ':') != 0) {
        return -1;
    }
    char numbuf[64];
    int len = snprintf(numbuf, sizeof(numbuf), "%lld", value);
    if (len < 0 || (size_t)len >= sizeof(numbuf)) {
        return -1;
    }
    if (buffer_append(buffer, numbuf, (size_t)len) != 0) {
        return -1;
    }
    return 0;
}

static int buffer_add_uint64(fmt_buffer *buffer, const char *name, uint64_t value) {
    if (buffer_field_prefix(buffer) != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    if (buffer_append(buffer, name, strlen(name)) != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, ':') != 0) {
        return -1;
    }
    char numbuf[64];
    int len = snprintf(numbuf, sizeof(numbuf), "%llu", (unsigned long long)value);
    if (len < 0 || (size_t)len >= sizeof(numbuf)) {
        return -1;
    }
    if (buffer_append(buffer, numbuf, (size_t)len) != 0) {
        return -1;
    }
    return 0;
}

static int buffer_add_double(fmt_buffer *buffer, const char *name, double value) {
    if (buffer_field_prefix(buffer) != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    if (buffer_append(buffer, name, strlen(name)) != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '"') != 0) {
        return -1;
    }
    if (buffer_append_char(buffer, ':') != 0) {
        return -1;
    }
    char dbl[64];
    if (fmt_print_double(dbl, sizeof(dbl), value) < 0) {
        return -1;
    }
    if (buffer_append(buffer, dbl, strlen(dbl)) != 0) {
        return -1;
    }
    return 0;
}

static int buffer_add_votes(fmt_buffer *buffer, const double votes[KOLIBRI_VOTE_COUNT]) {
    if (buffer_field_prefix(buffer) != 0) {
        return -1;
    }
    if (buffer_append_raw(buffer, "\"votes\":[") != 0) {
        return -1;
    }
    for (size_t i = 0; i < KOLIBRI_VOTE_COUNT; i++) {
        char dbl[64];
        if (fmt_print_double(dbl, sizeof(dbl), votes[i]) < 0) {
            return -1;
        }
        if (i > 0) {
            if (buffer_append_char(buffer, ',') != 0) {
                return -1;
            }
        }
        if (buffer_append(buffer, dbl, strlen(dbl)) != 0) {
            return -1;
        }
    }
    if (buffer_append_char(buffer, ']') != 0) {
        return -1;
    }
    return 0;
}

static int buffer_finish(fmt_buffer *buffer, size_t *written) {
    if (buffer->error) {
        return -1;
    }
    if (buffer->size == 0) {
        return -1;
    }
    if (buffer_append_char(buffer, '}') != 0) {
        return -1;
    }
    if (written != NULL) {
        *written = buffer->len;
    }
    return 0;
}

int fmt_build_json(const ReasonBlock *block,
                   int include_crypto,
                   const char *hash_hex,
                   const char *hmac_hex,
                   char *out,
                   size_t out_size,
                   size_t *written) {
    if (out == NULL || out_size == 0 || block == NULL) {
        return -1;
    }
    fmt_buffer buffer;
    buffer_init(&buffer, out, out_size);
    if (buffer.size < 2) {
        return -1;
    }
    if (buffer_add_int(&buffer, "step", block->step) != 0) {
        return -1;
    }
    if (buffer_add_int(&buffer, "parent", block->parent) != 0) {
        return -1;
    }
    if (buffer_add_uint64(&buffer, "seed", block->seed) != 0) {
        return -1;
    }
    if (buffer_add_string(&buffer, "formula", block->formula) != 0) {
        return -1;
    }
    if (buffer_add_double(&buffer, "eff", block->eff) != 0) {
        return -1;
    }
    if (buffer_add_double(&buffer, "compl", block->compl) != 0) {
        return -1;
    }
    if (buffer_add_string(&buffer, "prev", block->prev) != 0) {
        return -1;
    }
    if (buffer_add_votes(&buffer, block->votes) != 0) {
        return -1;
    }
    if (buffer_add_int(&buffer, "fmt", block->fmt) != 0) {
        return -1;
    }
    if (buffer_add_string(&buffer, "fa", block->fa) != 0) {
        return -1;
    }
    if (buffer_add_int(&buffer, "fa_stab", block->fa_stab) != 0) {
        return -1;
    }
    if (buffer_add_string(&buffer, "fa_map", block->fa_map) != 0) {
        return -1;
    }
    if (buffer_add_double(&buffer, "r", block->r) != 0) {
        return -1;
    }
    if (buffer_add_string(&buffer, "run_id", block->run_id) != 0) {
        return -1;
    }
    if (buffer_add_string(&buffer, "cfg_hash", block->cfg_hash) != 0) {
        return -1;
    }
    if (buffer_add_double(&buffer, "eff_train", block->eff_train) != 0) {
        return -1;
    }
    if (buffer_add_double(&buffer, "eff_val", block->eff_val) != 0) {
        return -1;
    }
    if (buffer_add_double(&buffer, "eff_test", block->eff_test) != 0) {
        return -1;
    }
    if (buffer_add_string(&buffer, "explain", block->explain) != 0) {
        return -1;
    }
    if (buffer_add_string(&buffer, "hmac_alg", block->hmac_alg) != 0) {
        return -1;
    }
    if (buffer_add_string(&buffer, "salt", block->salt) != 0) {
        return -1;
    }
    if (include_crypto) {
        if (hash_hex == NULL || hmac_hex == NULL) {
            return -1;
        }
        if (buffer_add_string(&buffer, "hash", hash_hex) != 0) {
            return -1;
        }
        if (buffer_add_string(&buffer, "hmac", hmac_hex) != 0) {
            return -1;
        }
    }
    return buffer_finish(&buffer, written);
}
