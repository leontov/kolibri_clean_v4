#ifndef KOLIBRI_FRACTAL_H
#define KOLIBRI_FRACTAL_H

#include <stddef.h>

#define FA10_DIGITS 10
#define FA10_LENGTH 10

typedef struct {
    double r;
    double transforms[FA10_DIGITS][2];
    char *raw_json;
    size_t raw_len;
} fractal_map_t;

int fractal_load_map(const char *path, fractal_map_t *map);
void fractal_free_map(fractal_map_t *map);
void fractal_encode_votes(const double votes[FA10_LENGTH], char out[FA10_LENGTH + 1]);
void fractal_apply_sequence(const fractal_map_t *map, const char fa[FA10_LENGTH + 1], double point[2]);
size_t fractal_stability(const char history[][FA10_LENGTH + 1], size_t history_len, size_t window);

#endif
