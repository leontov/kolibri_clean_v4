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
    char source_path[256];
    bool loaded_from_file;
    char canonical_json[256];
    char fingerprint[65];
    char hmac_key[128];
    char hmac_salt[128];
    bool has_hmac_key;
    bool has_hmac_salt;
} kolibri_config_t;

bool kolibri_load_config(kolibri_config_t* cfg, const char* json_path);
bool kolibri_config_write_snapshot(const kolibri_config_t* cfg, const char* path);
const char* kolibri_config_hmac_key(const kolibri_config_t* cfg);

#endif
