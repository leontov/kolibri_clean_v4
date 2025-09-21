#ifndef KOLIBRI_CHAINIO_H
#define KOLIBRI_CHAINIO_H
#include "reason.h"
#include <stdbool.h>
bool chain_append(const char* path, const ReasonBlock* b);
bool chain_load(const char* path, ReasonBlock** out_arr, size_t* out_n);
#endif
