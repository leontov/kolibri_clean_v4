#include "engine.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "digit.h"
#include "persist.h"

extern int kol_language_generate(char *buf, size_t cap);

static void append_fmt(char *buf, size_t cap, size_t *pos, const char *fmt, ...) {
    if (!buf || !pos || cap == 0) {
        return;
    }
    if (*pos >= cap) {
        *pos = cap - 1;
        buf[cap - 1] = '\0';
        return;
    }
    va_list args;
    va_start(args, fmt);
    size_t available = cap - *pos;
    if (available == 0) {
        *pos = cap - 1;
        buf[cap - 1] = '\0';
        va_end(args);
        return;
    }
    int written = vsnprintf(buf + *pos, available, fmt, args);
    va_end(args);
    if (written < 0) {
        buf[*pos] = '\0';
        return;
    }
    if ((size_t)written >= available) {
        *pos = cap - 1;
        buf[cap - 1] = '\0';
        return;
    }
    *pos += (size_t)written;
}

static void append_utf8_clipped(char *buf, size_t cap, size_t *pos, const char *src) {
    if (!buf || !pos || !src || cap == 0) {
        return;
    }
    if (*pos >= cap) {
        *pos = cap - 1;
        buf[cap - 1] = '\0';
        return;
    }
    size_t available = cap - *pos;
    if (available <= 1) {
        buf[cap - 1] = '\0';
        *pos = cap - 1;
        return;
    }
    size_t idx = 0u;
    while (src[idx] != '\0' && available > 1u) {
        unsigned char ch = (unsigned char)src[idx];
        size_t        char_len = 1u;
        if ((ch & 0x80u) == 0u) {
            char_len = 1u;
        } else if ((ch & 0xE0u) == 0xC0u) {
            char_len = 2u;
        } else if ((ch & 0xF0u) == 0xE0u) {
            char_len = 3u;
        } else if ((ch & 0xF8u) == 0xF0u) {
            char_len = 4u;
        }
        if (char_len > available - 1u) {
            break;
        }
        memcpy(buf + *pos, src + idx, char_len);
        *pos += char_len;
        available -= char_len;
        idx += char_len;
    }
    buf[*pos] = '\0';
}

static size_t extract_memory_keywords(const char *summary, char words[][64], size_t max_words) {
    if (!summary || !words || max_words == 0u) {
        return 0u;
    }
    size_t count = 0u;
    const char *ptr = summary;
    while (count < max_words) {
        const char *bullet = strstr(ptr, "• ");
        if (!bullet) {
            break;
        }
        bullet += strlen("• ");
        const char *stop = strstr(bullet, " ×");
        if (!stop) {
            break;
        }
        size_t len = (size_t)(stop - bullet);
        if (len >= 64u) {
            len = 63u;
        }
        memcpy(words[count], bullet, len);
        words[count][len] = '\0';
        while (len > 0u && (unsigned char)words[count][len - 1u] <= ' ') {
            --len;
            words[count][len] = '\0';
        }
        ++count;
        ptr = stop;
    }
    return count;
}

static void init_dataset(KolEngine *engine) {
    size_t n = sizeof(engine->xs) / sizeof(engine->xs[0]);
    for (size_t i = 0; i < n; ++i) {
        double t = -1.0 + 2.0 * (double)i / (double)(n - 1);
        engine->xs[i] = t;
        engine->ys[i] = sin(t * 3.141592653589793);
    }
    engine->dataset.xs = engine->xs;
    engine->dataset.ys = engine->ys;
    engine->dataset.count = n;
}

static uint8_t encode_sample(double value) {
    double normalized = (value + 1.0) * 0.5;
    if (normalized < 0.0) {
        normalized = 0.0;
    }
    if (normalized > 1.0) {
        normalized = 1.0;
    }
    long scaled = lrint(normalized * 9.0);
    if (scaled < 0) {
        scaled = 0;
    }
    if (scaled > 9) {
        scaled = 9;
    }
    return (uint8_t)scaled;
}

static size_t compute_digits(KolEngine *engine, uint8_t *buffer, size_t capacity) {
    if (!engine || !buffer || capacity == 0 || !engine->current) {
        if (buffer && capacity > 0) {
            memset(buffer, 0, capacity);
        }
        return 0;
    }
    size_t limit = engine->dataset.count;
    if (limit > capacity) {
        limit = capacity;
    }
    for (size_t i = 0; i < limit; ++i) {
        double pred = dsl_eval(engine->current, engine->dataset.xs[i]);
        buffer[i] = encode_sample(pred);
    }
    if (limit < capacity) {
        memset(buffer + limit, 0, capacity - limit);
    }
    return limit;
}

static void digits_to_text(const uint8_t *digits, size_t len, char *buf, size_t cap) {
    if (!buf || cap == 0) {
        return;
    }
    buf[0] = '\0';
    size_t pos = 0;

    char language_summary[512];
    language_summary[0] = '\0';
    int lang_len = kol_language_generate(language_summary, sizeof(language_summary));
    if (lang_len < 0) {
        language_summary[0] = '\0';
    }

    static const char *PHASES[] = {"набл.", "гип.", "реш."};
    static const char *DIGIT_TRAITS[] = {"тишина", "искры",  "ритм",  "волны", "фокус",
                                         "узор",   "связь", "резон", "ускор", "свет"};
    static const char *PEAK_NOTES[] = {"тихо",   "мягко", "ритм",  "волнует", "фокус",
                                       "структура", "согласие", "резонанс", "ускор", "ярко"};

    if (!digits || len == 0) {
        append_fmt(buf, cap, &pos, "Узор пока не проявлен.");
    } else {
        append_fmt(buf, cap, &pos, "Узор: ");
        size_t segment_count = len < 1 ? len : 1;
        for (size_t i = 0; i < segment_count; ++i) {
            unsigned int value = digits[i] % 10u;
            double       intensity = (double)value / 9.0 * 100.0;
            if (intensity < 0.0) {
                intensity = 0.0;
            }
            if (intensity > 100.0) {
                intensity = 100.0;
            }
            const char *phase = PHASES[i % (sizeof(PHASES) / sizeof(PHASES[0]))];
            const char *trait = DIGIT_TRAITS[value % (sizeof(DIGIT_TRAITS) / sizeof(DIGIT_TRAITS[0]))];
            append_fmt(buf, cap, &pos, "%s%s=%s %.0f%%", i > 0 ? "; " : "", phase, trait,
                       intensity);
        }
        append_fmt(buf, cap, &pos, ". ");

        double  sum = 0.0;
        uint8_t peak = 0u;
        for (size_t i = 0; i < len; ++i) {
            sum += digits[i];
            if (digits[i] > peak) {
                peak = digits[i];
            }
        }
        double mean = len > 0 ? sum / (double)len : 0.0;
        double mean_percent = (mean / 9.0) * 100.0;
        double peak_percent = ((double)peak / 9.0) * 100.0;
        if (mean_percent < 0.0) {
            mean_percent = 0.0;
        }
        if (mean_percent > 100.0) {
            mean_percent = 100.0;
        }
        if (peak_percent < 0.0) {
            peak_percent = 0.0;
        }
        if (peak_percent > 100.0) {
            peak_percent = 100.0;
        }
        const char *note = PEAK_NOTES[peak % (sizeof(PEAK_NOTES) / sizeof(PEAK_NOTES[0]))];
        append_fmt(buf, cap, &pos, "Ср %.0f%% | Пик %.0f%% (%s).", mean_percent, peak_percent,
                   note);
    }

    if (language_summary[0] != '\0') {
        char   keywords[3][64];
        size_t keyword_count = extract_memory_keywords(language_summary, keywords, 3u);
        if (keyword_count > 0u) {
            append_fmt(buf, cap, &pos, "\nПамять: %s.", keywords[0]);
        } else {
            append_fmt(buf, cap, &pos, "\nПамять: ");
            append_utf8_clipped(buf, cap, &pos, language_summary);
        }
    } else {
        append_fmt(buf, cap, &pos, "\nПамять: ждёт новых сигналов.");
    }

    if (cap > 0) {
        buf[cap - 1] = '\0';
    }
}

static void engine_refresh_output(KolEngine *engine) {
    if (!engine) {
        return;
    }
    size_t produced = compute_digits(engine, engine->last_digits, sizeof(engine->last_digits));
    engine->last_digit_count = produced;
    digits_to_text(engine->last_digits, produced, engine->last_text, sizeof(engine->last_text));
}

KolEngine *engine_create(uint8_t depth, uint32_t seed) {
    KolEngine *engine = (KolEngine *)calloc(1, sizeof(KolEngine));
    if (!engine) {
        return NULL;
    }
    init_dataset(engine);
    engine->fractal = fractal_create(depth, seed);
    if (!engine->fractal) {
        free(engine);
        return NULL;
    }
    uint32_t rng_state = seed ? seed : 1234u;
    engine->current = dsl_rand(&rng_state, 3);
    engine->last = metrics_eval(engine->current, &engine->dataset);
    engine->step = 0;
    memset(engine->last_digits, 0, sizeof(engine->last_digits));
    engine->last_digit_count = 0;
    engine->last_text[0] = '\0';
    engine_refresh_output(engine);
    return engine;
}

void engine_free(KolEngine *engine) {
    if (!engine) {
        return;
    }
    dsl_free(engine->current);
    fractal_free(engine->fractal);
    free(engine);
}

static KolDigit *get_digit(KolDigit *root, uint8_t idx) {
    if (!root) {
        return NULL;
    }
    if (root->children[idx]) {
        return root->children[idx];
    }
    return root;
}

static KolFormula *choose_candidate(KolEngine *engine, KolDigit *leader_digit, uint8_t leader_id) {
    KolFormula *candidate = NULL;
    if (leader_digit) {
        const KolExperience *best = digit_best_experience(leader_digit);
        if (best && best->formula) {
            double bias = rng_normalized(&leader_digit->rng);
            uint32_t state = leader_digit->rng.state;
            if (bias > 0.25) {
                candidate = dsl_mutate(best->formula, &state);
            }
            if (!candidate) {
                candidate = dsl_clone(best->formula);
            }
            leader_digit->rng.state = state;
        }
    }
    uint32_t fallback_state = leader_digit ? leader_digit->rng.state : (engine->step + 1u) * 811u;
    if (!candidate) {
        if (engine->current && leader_id < 4) {
            candidate = dsl_mutate(engine->current, &fallback_state);
        } else if (engine->current && leader_id < 7) {
            candidate = dsl_simplify(engine->current);
        } else {
            candidate = dsl_rand(&fallback_state, 3);
        }
        if (leader_digit) {
            leader_digit->rng.state = fallback_state;
        }
    }
    if (!candidate && engine->current) {
        candidate = dsl_clone(engine->current);
    }
    return candidate;
}

int engine_tick(KolEngine *engine, const KolEvent *in, KolOutput *out) {
    if (!engine) {
        return -1;
    }
    if (in && in->length > 0) {
        size_t count = engine->dataset.count;
        for (size_t i = 0; i < count && i < in->length; ++i) {
            engine->ys[i] = -1.0 + 2.0 * ((double)in->digits[i] / 9.0);
        }
    }
    KolDigit *root = fractal_root(engine->fractal);
    digit_self_train(root, &engine->dataset);
    KolDigit *digits[10];
    for (uint8_t i = 0; i < 10; ++i) {
        digits[i] = get_digit(root, i);
    }
    KolState state = {engine->current, engine->last, engine->step};
    KolVote vote = vote_run(digits, &state);
    KolDigit *leader_digit = digits[vote.leader_id];
    KolFormula *candidate = choose_candidate(engine, leader_digit, vote.leader_id);
    if (!candidate) {
        return -1;
    }
    KolMetrics cand_metrics = metrics_eval(candidate, &engine->dataset);
    engine->step += 1;
    int adopt = 0;
    if (cand_metrics.eff >= engine->last.eff || vote.scores[vote.leader_id] > 0.7f) {
        adopt = 1;
    }
    if (adopt) {
        dsl_free(engine->current);
        engine->current = candidate;
        engine->last = cand_metrics;
        if (leader_digit) {
            digit_learn(leader_digit, engine->current, &engine->last);
        }
    } else {
        dsl_free(candidate);
    }
    KolBlock block;
    memset(&block, 0, sizeof(block));
    block.step = engine->step;
    block.digit_id = vote.leader_id;
    char *formula_str = dsl_print(engine->current);
    if (formula_str) {
        strncpy(block.formula, formula_str, sizeof(block.formula) - 1);
        block.formula[sizeof(block.formula) - 1] = '\0';
        free(formula_str);
    }
    block.eff = engine->last.eff;
    block.compl = engine->last.compl;
    block.ts = persist_timestamp();
    chain_append(&block);
    engine_refresh_output(engine);
    if (out) {
        strncpy(out->formula, block.formula, sizeof(out->formula) - 1);
        out->formula[sizeof(out->formula) - 1] = '\0';
        out->metrics = engine->last;
        out->leader = vote.leader_id;
        out->digit_count = engine->last_digit_count;
        if (out->digit_count > sizeof(out->digits) / sizeof(out->digits[0])) {
            out->digit_count = sizeof(out->digits) / sizeof(out->digits[0]);
        }
        if (out->digit_count > 0) {
            memcpy(out->digits, engine->last_digits, out->digit_count);
        }
        if (out->digit_count < sizeof(out->digits) / sizeof(out->digits[0])) {
            memset(out->digits + out->digit_count, 0,
                   sizeof(out->digits) / sizeof(out->digits[0]) - out->digit_count);
        }
        strncpy(out->text, engine->last_text, sizeof(out->text) - 1);
        out->text[sizeof(out->text) - 1] = '\0';
    }
    return 0;
}

int engine_ingest_text(KolEngine *engine, const char *utf8, KolEvent *out_event) {
    (void)engine;
    if (!out_event || !utf8) {
        return -1;
    }
    uint8_t digits[sizeof(out_event->digits)];
    size_t len = 0;
    size_t capacity = sizeof(digits) / sizeof(digits[0]);
    while (utf8[len] && len < capacity) {
        digits[len] = (uint8_t)(((unsigned char)utf8[len]) % 10u);
        ++len;
    }
    return engine_ingest_digits(engine, digits, len, out_event);
}

int engine_ingest_digits(KolEngine *engine, const uint8_t *digits, size_t len, KolEvent *out_event) {
    (void)engine;
    if (!digits || !out_event) {
        return -1;
    }
    size_t capacity = sizeof(out_event->digits) / sizeof(out_event->digits[0]);
    size_t count = len < capacity ? len : capacity;
    memset(out_event->digits, 0, sizeof(out_event->digits));
    for (size_t i = 0; i < count; ++i) {
        out_event->digits[i] = (uint8_t)(digits[i] % 10u);
    }
    out_event->length = count;
    return 0;
}

int engine_ingest_bytes(KolEngine *engine, const uint8_t *bytes, size_t len, KolEvent *out_event) {
    (void)engine;
    if (!bytes || !out_event) {
        return -1;
    }
    size_t capacity = sizeof(out_event->digits) / sizeof(out_event->digits[0]);
    size_t idx = 0;
    memset(out_event->digits, 0, sizeof(out_event->digits));
    for (size_t i = 0; i < len && idx + 3 <= capacity; ++i) {
        uint8_t value = bytes[i];
        out_event->digits[idx++] = (uint8_t)((value / 100u) % 10u);
        out_event->digits[idx++] = (uint8_t)((value / 10u) % 10u);
        out_event->digits[idx++] = (uint8_t)(value % 10u);
    }
    out_event->length = idx;
    return 0;
}

int engine_ingest_signal(KolEngine *engine, const float *samples, size_t len, KolEvent *out_event) {
    (void)engine;
    if (!samples || !out_event) {
        return -1;
    }
    size_t capacity = sizeof(out_event->digits) / sizeof(out_event->digits[0]);
    size_t count = len < capacity ? len : capacity;
    memset(out_event->digits, 0, sizeof(out_event->digits));
    for (size_t i = 0; i < count; ++i) {
        double normalized = ((double)samples[i] + 1.0) * 0.5;
        if (normalized < 0.0) {
            normalized = 0.0;
        }
        if (normalized > 1.0) {
            normalized = 1.0;
        }
        long scaled = lrint(normalized * 9.0);
        if (scaled < 0) {
            scaled = 0;
        }
        if (scaled > 9) {
            scaled = 9;
        }
        out_event->digits[i] = (uint8_t)scaled;
    }
    out_event->length = count;
    return 0;
}

int engine_render_digits(KolEngine *engine, uint8_t *digits, size_t max_len, size_t *out_len) {
    if (!engine || !digits || max_len == 0) {
        return -1;
    }
    if (engine->last_digit_count == 0 && engine->current) {
        engine_refresh_output(engine);
    }
    size_t copy = engine->last_digit_count;
    if (copy > max_len) {
        copy = max_len;
    }
    memcpy(digits, engine->last_digits, copy);
    if (out_len) {
        *out_len = copy;
    }
    if (copy < max_len) {
        memset(digits + copy, 0, max_len - copy);
    }
    return 0;
}

int engine_render_text(KolEngine *engine, char *buf, size_t cap) {
    if (!engine || !buf || cap == 0) {
        return -1;
    }
    if (engine->last_digit_count == 0 && engine->current) {
        engine_refresh_output(engine);
    }
    size_t text_len = strlen(engine->last_text);
    if (text_len >= cap) {
        memcpy(buf, engine->last_text, cap - 1);
        buf[cap - 1] = '\0';
        return (int)(cap - 1);
    }
    memcpy(buf, engine->last_text, text_len + 1);
    return (int)text_len;
}
