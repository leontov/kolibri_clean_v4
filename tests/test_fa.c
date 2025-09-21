#include "fractal.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    double votes1[KOLIBRI_FA_DIGITS];
    double votes2[KOLIBRI_FA_DIGITS];
    fractal_generate_votes(123456789ULL, votes1);
    fractal_generate_votes(123456789ULL, votes2);
    for (size_t i = 0; i < KOLIBRI_FA_DIGITS; i++) {
        if (votes1[i] != votes2[i]) {
            fprintf(stderr, "votes mismatch\n");
            return 1;
        }
    }
    char fa1[KOLIBRI_FA_DIGITS + 1];
    char fa2[KOLIBRI_FA_DIGITS + 1];
    fractal_votes_to_address(votes1, fa1);
    fractal_votes_to_address(votes2, fa2);
    if (strcmp(fa1, fa2) != 0) {
        fprintf(stderr, "address mismatch\n");
        return 1;
    }
    char window[5][KOLIBRI_FA_DIGITS + 1];
    strcpy(window[0], "7056172034");
    strcpy(window[1], "7056172039");
    strcpy(window[2], "7056179034");
    size_t stab = fractal_stability((const char (*)[KOLIBRI_FA_DIGITS + 1])window, 3);
    if (stab != 6) {
        fprintf(stderr, "expected 6 got %zu\n", stab);
        return 1;
    }
    printf("OK\n");
    return 0;
}
