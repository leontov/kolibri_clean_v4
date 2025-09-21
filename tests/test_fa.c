#include "fractal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    double votes[FA10_LENGTH];
    for (size_t i = 0; i < FA10_LENGTH; ++i) {
        votes[i] = (double)i / (double)(FA10_LENGTH - 1);
    }
    char fa[FA10_LENGTH + 1];
    fractal_encode_votes(votes, fa);
    assert(fa[0] == '0');
    assert(fa[9] == '9');
    fractal_map_t map;
    assert(fractal_load_map("configs/fractal_map.default.json", &map) == 0);
    double point[2];
    fractal_apply_sequence(&map, fa, point);
    assert(point[0] >= 0.0);
    assert(point[1] >= 0.0);
    char history[5][FA10_LENGTH + 1];
    for (size_t i = 0; i < 5; ++i) {
        strcpy(history[i], fa);
    }
    size_t stab = fractal_stability((const char (*)[FA10_LENGTH + 1])history, 5, 5);
    assert(stab == FA10_LENGTH);
    fractal_free_map(&map);
    return 0;
}
