#ifndef KOLIBRI_VOTE_H
#define KOLIBRI_VOTE_H

#include "digit.h"

typedef struct {
    uint8_t leader_id;
    float scores[10];
} KolVote;

KolVote vote_run(KolDigit *digits[10], const KolState *state);

#endif
