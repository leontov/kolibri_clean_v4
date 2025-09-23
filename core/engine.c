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

static void engine_update_stats(KolEngine *engine) {
    if (!engine) {
        return;
    }
    size_t n = engine->dataset.count;
    if (n == 0) {
        engine->dataset_mean = 0.0;
        engine->dataset_min = 0.0;
        engine->dataset_max = 0.0;
        return;
    }
    double sum = 0.0;
    double minv = engine->ys[0];
    double maxv = engine->ys[0];
    for (size_t i = 0; i < n; ++i) {
        double v = engine->ys[i];
        sum += v;
        if (v < minv) {
            minv = v;
        }
        if (v > maxv) {
            maxv = v;
        }
    }
    engine->dataset_mean = sum / (double)n;
    engine->dataset_min = minv;
    engine->dataset_max = maxv;
}

typedef struct {
    uint32_t codepoint;
    uint32_t count;
} KolCodepointFreq;

static uint32_t kol_hash32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

static uint8_t collapse_to_digit(uint32_t value) {
    return (uint8_t)(value % 10u);
}

static size_t engine_utf8_decode(const unsigned char *src, uint32_t *out_cp) {
    if (!src || !src[0]) {
        return 0u;
    }
    unsigned char c0 = src[0];
    if (c0 < 0x80u) {
        *out_cp = (uint32_t)c0;
        return 1u;
    }
    if ((c0 & 0xE0u) == 0xC0u) {
        unsigned char c1 = src[1];
        if ((c1 & 0xC0u) != 0x80u) {
            *out_cp = (uint32_t)c0;
            return 1u;
        }
        *out_cp = ((uint32_t)(c0 & 0x1Fu) << 6u) | (uint32_t)(c1 & 0x3Fu);
        return 2u;
    }
    if ((c0 & 0xF0u) == 0xE0u) {
        unsigned char c1 = src[1];
        unsigned char c2 = src[2];
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u) {
            *out_cp = (uint32_t)c0;
            return 1u;
        }
        *out_cp = ((uint32_t)(c0 & 0x0Fu) << 12u) | ((uint32_t)(c1 & 0x3Fu) << 6u) |
                  (uint32_t)(c2 & 0x3Fu);
        return 3u;
    }
    if ((c0 & 0xF8u) == 0xF0u) {
        unsigned char c1 = src[1];
        unsigned char c2 = src[2];
        unsigned char c3 = src[3];
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u) {
            *out_cp = (uint32_t)c0;
            return 1u;
        }
        *out_cp = ((uint32_t)(c0 & 0x07u) << 18u) | ((uint32_t)(c1 & 0x3Fu) << 12u) |
                  ((uint32_t)(c2 & 0x3Fu) << 6u) | (uint32_t)(c3 & 0x3Fu);
        return 4u;
    }
    *out_cp = (uint32_t)c0;
    return 1u;
}

static size_t encode_utf8_digits(const char *utf8, uint8_t *digits, size_t capacity,
                                 uint8_t *out_stride) {
    if (!utf8 || !digits || capacity == 0u) {
        if (out_stride) {
            *out_stride = 0u;
        }
        return 0u;
    }
    const size_t stride = 4u;
    KolCodepointFreq freq_table[64];
    size_t           freq_count = 0u;
    memset(freq_table, 0, sizeof(freq_table));
    const unsigned char *ptr = (const unsigned char *)utf8;
    size_t               idx = 0u;
    size_t               position = 0u;
    while (*ptr && idx + stride <= capacity) {
        uint32_t cp = 0u;
        size_t   adv = engine_utf8_decode(ptr, &cp);
        if (adv == 0u) {
            break;
        }
        size_t entry_index = 0u;
        int    found = 0;
        for (; entry_index < freq_count; ++entry_index) {
            if (freq_table[entry_index].codepoint == cp) {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (freq_count < sizeof(freq_table) / sizeof(freq_table[0])) {
                entry_index = freq_count++;
            } else {
                size_t weakest = 0u;
                uint32_t weakest_count = freq_table[0].count;
                for (size_t i = 1u; i < freq_count; ++i) {
                    if (freq_table[i].count < weakest_count) {
                        weakest = i;
                        weakest_count = freq_table[i].count;
                    }
                }
                entry_index = weakest;
            }
            freq_table[entry_index].codepoint = cp;
            freq_table[entry_index].count = 0u;
        }
        freq_table[entry_index].count += 1u;
        uint32_t freq = freq_table[entry_index].count;
        uint32_t hash = kol_hash32(cp);
        uint32_t freq_hash = kol_hash32(freq);
        uint32_t order_hash = kol_hash32((uint32_t)(position + 1u));
        uint8_t freq_low = collapse_to_digit(freq);
        uint8_t freq_mix = collapse_to_digit(freq / 10u + (uint32_t)(position % 10u) + freq_hash);
        uint8_t id_primary = collapse_to_digit(hash ^ order_hash);
        uint8_t id_trail =
            collapse_to_digit((hash >> 8u) + (freq_hash >> 11u) + order_hash + (uint32_t)position);
        digits[idx++] = freq_low;
        digits[idx++] = freq_mix;
        digits[idx++] = id_primary;
        digits[idx++] = id_trail;
        ptr += adv;
        position += 1u;
    }
    if (out_stride) {
        *out_stride = (idx > 0u) ? (uint8_t)stride : 0u;
    }
    if (idx < capacity) {
        memset(digits + idx, 0, capacity - idx);
    }
    return idx;


}

static void init_dataset(KolEngine *engine) {
    size_t n = sizeof(engine->xs) / sizeof(engine->xs[0]);
    engine->dataset.xs = engine->xs;
    engine->dataset.ys = engine->ys;
    engine->dataset.count = n;
    for (size_t i = 0; i < n; ++i) {
        double t = -1.0 + 2.0 * (double)i / (double)(n - 1);
        engine->xs[i] = t;
        double y = sin(t * 3.141592653589793);
        engine->baseline[i] = y;
        engine->ys[i] = y;
    }
    memset(engine->obs_values, 0, sizeof(engine->obs_values));
    engine->obs_count = 0;
    engine->obs_head = 0;
    memset(engine->event_buffer, 0, sizeof(engine->event_buffer));
    engine->event_count = 0;
    engine->event_head = 0;
    engine_update_stats(engine);
}

static double digit_to_value(uint8_t digit) {
    double normalized = (double)digit / 9.0;
    if (normalized < 0.0) {
        normalized = 0.0;
    }
    if (normalized > 1.0) {
        normalized = 1.0;
    }
    return -1.0 + 2.0 * normalized;
}

static size_t obs_capacity(const KolEngine *engine) {
    if (!engine) {
        return 0;
    }
    return sizeof(engine->obs_values) / sizeof(engine->obs_values[0]);
}

static double observation_at(const KolEngine *engine, size_t idx) {
    if (!engine || engine->obs_count == 0 || idx >= engine->obs_count) {
        return 0.0;
    }
    size_t cap = obs_capacity(engine);
    size_t pos = (engine->obs_head + idx) % cap;
    return engine->obs_values[pos];
}

static void push_observation(KolEngine *engine, double value) {
    if (!engine) {
        return;
    }
    size_t cap = obs_capacity(engine);
    if (cap == 0) {
        return;
    }
    if (engine->obs_count < cap) {
        size_t pos = (engine->obs_head + engine->obs_count) % cap;
        engine->obs_values[pos] = value;
        engine->obs_count += 1;
    } else {
        engine->obs_values[engine->obs_head] = value;
        engine->obs_head = (engine->obs_head + 1) % cap;
    }
}

static void engine_record_event(KolEngine *engine, const KolEvent *event) {
    if (!engine || !event || event->length == 0) {
        return;
    }
    size_t cap = sizeof(engine->event_buffer) / sizeof(engine->event_buffer[0]);
    if (cap == 0) {
        return;
    }
    size_t insert_pos = (engine->event_head + engine->event_count) % cap;
    engine->event_buffer[insert_pos] = *event;
    if (event->length < sizeof(event->digits) / sizeof(event->digits[0])) {
        size_t tail = sizeof(event->digits) / sizeof(event->digits[0]) - event->length;
        memset(engine->event_buffer[insert_pos].digits + event->length, 0, tail);
    }
    if (engine->event_count < cap) {
        engine->event_count += 1;
    } else {
        engine->event_head = (engine->event_head + 1) % cap;
    }
    for (size_t i = 0; i < event->length; ++i) {
        double value = digit_to_value(event->digits[i]);
        push_observation(engine, value);
    }
}

static void engine_prepare_dataset(KolEngine *engine) {
    if (!engine) {
        return;
    }
    size_t n = engine->dataset.count;
    if (n == 0) {
        engine->dataset_mean = 0.0;
        engine->dataset_min = 0.0;
        engine->dataset_max = 0.0;
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        engine->ys[i] = engine->baseline[i];
    }
    size_t real_count = engine->obs_count;
    if (real_count > n) {
        real_count = n;
    }
    if (real_count > 0) {
        double raw_min = observation_at(engine, 0);
        double raw_max = raw_min;
        for (size_t i = 1; i < engine->obs_count; ++i) {
            double v = observation_at(engine, i);
            if (v < raw_min) {
                raw_min = v;
            }
            if (v > raw_max) {
                raw_max = v;
            }
        }
        if (raw_max - raw_min < 1e-9) {
            raw_max = raw_min + 1e-9;
        }
        size_t start = n - real_count;
        for (size_t i = 0; i < real_count; ++i) {
            size_t obs_index = engine->obs_count - real_count + i;
            double raw = observation_at(engine, obs_index);
            double norm = (raw - raw_min) / (raw_max - raw_min);
            norm = norm * 2.0 - 1.0;
            engine->ys[start + i] = norm;
        }
    }
    engine_update_stats(engine);
}

void engine_reset_dataset(KolEngine *engine) {
    if (!engine) {
        return;
    }
    memset(engine->obs_values, 0, sizeof(engine->obs_values));
    engine->obs_count = 0;
    engine->obs_head = 0;
    memset(engine->event_buffer, 0, sizeof(engine->event_buffer));
    engine->event_count = 0;
    engine->event_head = 0;
    for (size_t i = 0; i < engine->dataset.count; ++i) {
        engine->ys[i] = engine->baseline[i];
    }
    engine_update_stats(engine);
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
        engine_record_event(engine, in);
    }
    engine_prepare_dataset(engine);
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
        persist_quantize_formula(block.formula, sizeof(block.formula));
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
    KolPersistState snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.step = engine->step;
    snapshot.metrics = engine->last;
    snapshot.dataset_mean = engine->dataset_mean;
    snapshot.dataset_min = engine->dataset_min;
    snapshot.dataset_max = engine->dataset_max;
    persist_save_state(&snapshot);
    return 0;
}

int engine_ingest_text(KolEngine *engine, const char *utf8, KolEvent *out_event) {
    (void)engine;
    if (!out_event || !utf8) {
        return -1;
    }
    memset(out_event->digits, 0, sizeof(out_event->digits));
    size_t produced = encode_utf8_digits(utf8, out_event->digits,
                                         sizeof(out_event->digits) / sizeof(out_event->digits[0]),
                                         &out_event->stride);
    out_event->length = produced;
    if (produced == 0) {
        out_event->stride = 0;
    }
    return 0;
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
    out_event->stride = count > 0 ? 1u : 0u;
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
    const size_t stride = 3u;
    for (size_t i = 0; i < len && idx + stride <= capacity; ++i) {
        uint8_t value = bytes[i];
        out_event->digits[idx++] = (uint8_t)((value / 100u) % 10u);
        out_event->digits[idx++] = (uint8_t)((value / 10u) % 10u);
        out_event->digits[idx++] = (uint8_t)(value % 10u);
    }
    out_event->length = idx;
    out_event->stride = idx > 0 ? (uint8_t)stride : 0u;
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
    out_event->stride = count > 0 ? 1u : 0u;
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
