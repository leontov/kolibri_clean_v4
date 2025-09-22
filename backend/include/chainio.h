#ifndef KOLIBRI_CHAINIO_H
#define KOLIBRI_CHAINIO_H
#include "config.h"
#include "reason.h"
#include <stdbool.h>
#include <stdio.h>
bool chain_append(const char* path, const ReasonBlock* b, const kolibri_config_t* cfg);
bool chain_load(const char* path, ReasonBlock** out_arr, size_t* out_n);

typedef struct {
    uint64_t height;
    char head_hash[65];
    char prev_hash[65];
    char fingerprint[65];
} kolibri_chain_summary_t;

typedef bool (*chain_stream_callback)(const char* line, const ReasonBlock* block, void* user);

bool chain_parse_line(const char* line, ReasonBlock* out);
bool chain_validate_block(const ReasonBlock* block, const kolibri_config_t* cfg, const char* expected_prev_hash);
bool chain_get_summary(const char* path, kolibri_chain_summary_t* out, const kolibri_config_t* cfg);
bool chain_stream_from(const char* path, uint64_t start_step, chain_stream_callback cb, void* user);
bool chain_verify(const char* path, FILE* out, const kolibri_config_t* cfg);
#endif
