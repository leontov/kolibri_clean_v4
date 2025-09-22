#include "vote.h"

#include <float.h>

KolVote vote_run(KolDigit *digits[10], const KolState *state) {
    KolVote vote;
    vote.leader_id = 0;
    for (int i = 0; i < 10; ++i) {
        vote.scores[i] = 0.0f;
    }
    float best = -FLT_MAX;
    for (int i = 0; i < 10; ++i) {
        if (!digits[i]) {
            continue;
        }
        float score = digit_vote(digits[i], state);
        vote.scores[i] = score;
        if (score > best) {
            best = score;
            vote.leader_id = (uint8_t)i;
        }
    }
    return vote;
}
