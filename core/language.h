#ifndef KOLIBRI_LANGUAGE_H
#define KOLIBRI_LANGUAGE_H

#include <stddef.h>
#include <stdint.h>

#define KOL_LANGUAGE_MAX_WORDS 128
#define KOL_LANGUAGE_MAX_WORD_LEN 64

typedef struct {
    char     word[KOL_LANGUAGE_MAX_WORD_LEN];
    uint32_t count;
} KolLanguageWord;

typedef struct {
    KolLanguageWord words[KOL_LANGUAGE_MAX_WORDS];
    size_t          word_count;
} KolLanguage;

void language_reset(KolLanguage *lang);
void language_observe(KolLanguage *lang, const char *utf8);
int  language_generate(const KolLanguage *lang, char *buf, size_t cap);

#endif
