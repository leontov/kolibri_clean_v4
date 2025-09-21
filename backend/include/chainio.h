#ifndef KOLIBRI_CHAINIO_H
#define KOLIBRI_CHAINIO_H
#include "config.h"
#include "reason.h"
#include <stdbool.h>
#include <stdio.h>
bool chain_append(const char* path, const ReasonBlock* b, const kolibri_config_t* cfg);
bool chain_load(const char* path, ReasonBlock** out_arr, size_t* out_n);
bool chain_verify(const char* path, FILE* out, const kolibri_config_t* cfg);
#endif
