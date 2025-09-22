#ifndef KOLIBRI_API_H
#define KOLIBRI_API_H

#include <stddef.h>
#include <stdint.h>

#include "engine.h"

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

#endif
