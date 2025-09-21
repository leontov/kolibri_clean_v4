#ifndef KOLIBRI_CONFIG_H
#define KOLIBRI_CONFIG_H
#include "common.h"

typedef struct {
    int steps;
    int depth_max;
    double depth_decay;
    double quorum;
    double temperature;
    double eff_threshold;
    double max_complexity;
    uint64_t seed;
} kolibri_config_t;

bool kolibri_load_config(kolibri_config_t* cfg, const char* json_path);

#endif
