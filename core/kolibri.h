#ifndef KOLIBRI_API_H
#define KOLIBRI_API_H

#include <stddef.h>
#include <stdint.h>

#include "engine.h"

int    kol_init(uint8_t depth, uint32_t seed);
void   kol_reset(void);
int    kol_tick(void);
int    kol_chat_push(const char *text);
double kol_eff(void);
double kol_compl(void);
int    kol_tail_json(char *buf, int cap, int n);
void  *kol_alloc(size_t size);
void   kol_free(void *ptr);

#endif
