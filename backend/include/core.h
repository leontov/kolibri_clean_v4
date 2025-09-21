#ifndef KOLIBRI_CORE_H
#define KOLIBRI_CORE_H
#include "config.h"
#include "reason.h"
#include <stdbool.h>

bool kolibri_step(const kolibri_config_t* cfg, int step, const char* prev_hash,
                  ReasonBlock* out, char out_hash[65]);

#endif
