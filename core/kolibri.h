#ifndef KOLIBRI_API_H
#define KOLIBRI_API_H

#include <stddef.h>
#include <stdint.h>

#include "engine.h"

typedef struct {
    uint32_t start_step;
    uint32_t executed;
    double   final_eff;
    double   final_compl;
    double   best_eff;
    double   best_compl;
    uint32_t best_step;
    char     best_formula[256];
} KolBootstrapReport;

int  kol_init(uint8_t depth, uint32_t seed);
void kol_reset(void);
int  kol_tick(void);
int  kol_chat_push(const char *text);
int  kol_bootstrap(int steps, KolBootstrapReport *report);


int    kol_init(uint8_t depth, uint32_t seed);
void   kol_reset(void);
int    kol_tick(void);
int    kol_chat_push(const char *text);
int    kol_ingest_digits(const uint8_t *digits, size_t len);
int    kol_ingest_bytes(const uint8_t *bytes, size_t len);
int    kol_ingest_signal(const float *samples, size_t len);

double kol_eff(void);
double kol_compl(void);
int    kol_tail_json(char *buf, int cap, int n);
int    kol_emit_digits(uint8_t *digits, size_t max_len, size_t *out_len);
int    kol_emit_text(char *buf, size_t cap);
void  *kol_alloc(size_t size);
void   kol_free(void *ptr);
int    kol_language_generate(char *buf, size_t cap);

#endif
