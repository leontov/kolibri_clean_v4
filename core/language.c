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
    char list[256];
    size_t list_len = 0u;
    list[0] = '\0';
    for (size_t j = 0; j < 3; ++j) {
        if (top_idx[j] == SIZE_MAX) {
            continue;
        }
        const char *word = lang->words[top_idx[j]].word;
        if (!word[0]) {
            continue;
        }
        if (list_len > 0u) {
            if (list_len + 2u >= sizeof(list)) {
                break;
            }
            list[list_len++] = ',';
            list[list_len++] = ' ';
            list[list_len] = '\0';
        }
        size_t word_len = strlen(word);
        if (list_len + word_len >= sizeof(list)) {
            break;
        }
        memcpy(&list[list_len], word, word_len);
        list_len += word_len;
        list[list_len] = '\0';
    }
    if (list_len == 0u) {
        int fallback = snprintf(buf, cap, "%s", KOL_LANGUAGE_DEFAULT_MESSAGE);
        if (fallback < 0 || (size_t)fallback >= cap) {
            return -1;
        }
        return fallback;
    }
    int written = snprintf(buf, cap, "Kolibri запомнил: %s", list);
    if (written < 0 || (size_t)written >= cap) {
        return -1;
    }
    return written;
}
