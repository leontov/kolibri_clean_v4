#ifndef KOLIBRI_CHAINIO_H
#define KOLIBRI_CHAINIO_H
#include "reason.h"
#include <stdbool.h>
#include <stdio.h>
bool chain_append(const char* path, const ReasonBlock* b);
bool chain_load(const char* path, ReasonBlock** out_arr, size_t* out_n);
bool chain_verify(const char* path, FILE* out);
#endif
