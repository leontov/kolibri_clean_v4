#include "language.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KOL_LANGUAGE_DEFAULT_MESSAGE "Колибри пока молчит..."

static void store_word(KolLanguage *lang, const char *word) {
    if (!lang || !word || !word[0]) {
        return;
    }
    for (size_t i = 0; i < lang->word_count; ++i) {
        if (strcmp(lang->words[i].word, word) == 0) {
            if (lang->words[i].count < UINT32_MAX) {
                lang->words[i].count += 1u;
            }
            return;
        }
    }
    if (lang->word_count >= KOL_LANGUAGE_MAX_WORDS) {
        /* Prefer keeping existing frequent entries. */
        size_t weakest = 0;
        uint32_t weakest_count = lang->words[0].count;
        for (size_t i = 1; i < lang->word_count; ++i) {
            if (lang->words[i].count < weakest_count) {
                weakest = i;
                weakest_count = lang->words[i].count;
            }
        }
        if (weakest_count > 1u) {
            return;
        }
        strncpy(lang->words[weakest].word, word, KOL_LANGUAGE_MAX_WORD_LEN - 1u);
        lang->words[weakest].word[KOL_LANGUAGE_MAX_WORD_LEN - 1u] = '\0';
        lang->words[weakest].count = 1u;
        return;
    }
    KolLanguageWord *slot = &lang->words[lang->word_count++];
    strncpy(slot->word, word, KOL_LANGUAGE_MAX_WORD_LEN - 1u);
    slot->word[KOL_LANGUAGE_MAX_WORD_LEN - 1u] = '\0';
    slot->count = 1u;
}

void language_reset(KolLanguage *lang) {
    if (!lang) {
        return;
    }
    memset(lang, 0, sizeof(*lang));
}

static int is_unicode_space(uint32_t cp) {
    if (cp <= 0x20u) {
        return 1;
    }
    switch (cp) {
        case 0x00A0u:
        case 0x1680u:
        case 0x180Eu:
        case 0x2000u:
        case 0x2001u:
        case 0x2002u:
        case 0x2003u:
        case 0x2004u:
        case 0x2005u:
        case 0x2006u:
        case 0x2007u:
        case 0x2008u:
        case 0x2009u:
        case 0x200Au:
        case 0x2028u:
        case 0x2029u:
        case 0x202Fu:
        case 0x205Fu:
        case 0x3000u:
            return 1;
        default:
            return 0;
    }
}

static int is_word_codepoint(uint32_t cp) {
    if (cp < 0x80u) {
        if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9') || cp == '_' || cp == '-') {
            return 1;
        }
        if (cp >= 'A' && cp <= 'Z') {
            return 1;
        }
        return 0;
    }
    if (is_unicode_space(cp)) {
        return 0;
    }
    if ((cp >= 0x2000u && cp <= 0x206Fu) || (cp >= 0x2E00u && cp <= 0x2E7Fu) ||
        (cp >= 0x3000u && cp <= 0x303Fu) || (cp >= 0xFE30u && cp <= 0xFE4Fu)) {
        return 0;
    }
    return 1;
}

static size_t utf8_decode(const unsigned char *src, uint32_t *out_cp) {
    if (!src || !src[0]) {
        return 0;
    }
    unsigned char c0 = src[0];
    if (c0 < 0x80u) {
        *out_cp = (uint32_t)c0;
        return 1u;
    }
    if ((c0 & 0xE0u) == 0xC0u) {
        unsigned char c1 = src[1];
        if ((c1 & 0xC0u) != 0x80u) {
            return 1u;
        }
        *out_cp = ((uint32_t)(c0 & 0x1Fu) << 6u) | (uint32_t)(c1 & 0x3Fu);
        return 2u;
    }
    if ((c0 & 0xF0u) == 0xE0u) {
        unsigned char c1 = src[1];
        unsigned char c2 = src[2];
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u) {
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
            return 1u;
        }
        *out_cp = ((uint32_t)(c0 & 0x07u) << 18u) | ((uint32_t)(c1 & 0x3Fu) << 12u) |
                  ((uint32_t)(c2 & 0x3Fu) << 6u) | (uint32_t)(c3 & 0x3Fu);
        return 4u;
    }
    return 1u;
}

void language_observe(KolLanguage *lang, const char *utf8) {
    if (!lang || !utf8) {
        return;
    }
    char   buffer[KOL_LANGUAGE_MAX_WORD_LEN];
    size_t buffer_len = 0u;
    const unsigned char *ptr = (const unsigned char *)utf8;
    while (*ptr) {
        uint32_t cp = 0u;
        size_t   adv = utf8_decode(ptr, &cp);
        if (adv == 0u) {
            break;
        }
        int is_word = is_word_codepoint(cp);
        if (!is_word && buffer_len > 0u) {
            buffer[buffer_len] = '\0';
            store_word(lang, buffer);
            buffer_len = 0u;
        }
        if (is_word) {
            if (buffer_len + adv >= sizeof(buffer)) {
                if (buffer_len > 0u) {
                    buffer[buffer_len] = '\0';
                    store_word(lang, buffer);
                    buffer_len = 0u;
                }
                if (adv >= sizeof(buffer)) {
                    ptr += adv;
                    continue;
                }
            }
            if (cp < 0x80u) {
                for (size_t i = 0; i < adv; ++i) {
                    unsigned char ch = ptr[i];
                    if (ch >= 'A' && ch <= 'Z') {
                        ch = (unsigned char)(ch - 'A' + 'a');
                    }
                    buffer[buffer_len++] = (char)ch;
                }
            } else {
                for (size_t i = 0; i < adv; ++i) {
                    buffer[buffer_len++] = (char)ptr[i];
                }
            }
        }
        ptr += adv;
    }
    if (buffer_len > 0u) {
        buffer[buffer_len] = '\0';
        store_word(lang, buffer);
    }
}

int language_generate(const KolLanguage *lang, char *buf, size_t cap) {
    if (!buf || cap == 0u) {
        return -1;
    }
    if (!lang || lang->word_count == 0u) {
        int fallback = snprintf(buf, cap, "%s", KOL_LANGUAGE_DEFAULT_MESSAGE);
        if (fallback < 0 || (size_t)fallback >= cap) {
            return -1;
        }
        return fallback;
    }
    size_t top_idx[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
    for (size_t i = 0; i < lang->word_count; ++i) {
        uint32_t current = lang->words[i].count;
        for (size_t j = 0; j < 3; ++j) {
            if (top_idx[j] == SIZE_MAX || current > lang->words[top_idx[j]].count) {
                for (size_t k = 2; k > j; --k) {
                    top_idx[k] = top_idx[k - 1];
                }
                top_idx[j] = i;
                break;
            }
        }
    }
    const KolLanguageWord *selected[3] = {NULL, NULL, NULL};
    uint32_t                counts[3] = {0u, 0u, 0u};
    size_t                  actual = 0u;
    for (size_t j = 0; j < 3; ++j) {
        if (top_idx[j] == SIZE_MAX) {
            continue;
        }
        const KolLanguageWord *word = &lang->words[top_idx[j]];
        if (!word->word[0]) {
            continue;
        }
        selected[actual] = word;
        counts[actual] = word->count;
        actual += 1u;
    }
    if (actual == 0u) {
        int fallback = snprintf(buf, cap, "%s", KOL_LANGUAGE_DEFAULT_MESSAGE);
        if (fallback < 0 || (size_t)fallback >= cap) {
            return -1;
        }
        return fallback;
    }
    uint32_t cluster_total = 0u;
    for (size_t i = 0; i < actual; ++i) {
        cluster_total += counts[i];
    }
    size_t offset = 0u;
    int written = snprintf(buf, cap, "Колибри выделяет темы:\n");
    if (written < 0) {
        return -1;
    }
    if ((size_t)written >= cap) {
        if (cap > 0u) {
            buf[cap - 1u] = '\0';
        }
        return cap > 0 ? (int)(cap - 1) : -1;
    }
    offset = (size_t)written;
    for (size_t i = 0; i < actual; ++i) {
        double share = 0.0;
        if (cluster_total > 0u) {
            share = (double)counts[i] * 100.0 / (double)cluster_total;
        }
        if (share < 0.0) {
            share = 0.0;
        }
        if (share > 100.0) {
            share = 100.0;
        }
        written = snprintf(buf + offset, cap - offset, "• %s ×%u (%.0f%%)\n",
                           selected[i]->word, (unsigned)counts[i], share);
        if (written < 0) {
            return -1;
        }
        if ((size_t)written >= cap - offset) {
            offset = cap > 0 ? cap - 1u : 0u;
            if (cap > 0u) {
                buf[offset] = '\0';
            }
            return (int)offset;
        }
        offset += (size_t)written;
    }
    const KolLanguageWord *primary = selected[0];
    double                 primary_share = 0.0;
    if (cluster_total > 0u) {
        primary_share = (double)counts[0] * 100.0 / (double)cluster_total;
        if (primary_share < 0.0) {
            primary_share = 0.0;
        }
        if (primary_share > 100.0) {
            primary_share = 100.0;
        }
    }
    const char *tone = "подсказывает направление";
    if (primary_share > 60.0) {
        tone = "ведёт диалог";
    } else if (primary_share > 30.0) {
        tone = "звучит отчётливо";
    }
    written = snprintf(buf + offset, cap - offset,
                       "Короткая мысль: \"%s\" %s.", primary->word, tone);
    if (written < 0) {
        return -1;
    }
    if ((size_t)written >= cap - offset) {
        offset = cap > 0 ? cap - 1u : 0u;
        if (cap > 0u) {
            buf[offset] = '\0';
        }
        return (int)offset;
    }
    offset += (size_t)written;
    return (int)offset;
}
