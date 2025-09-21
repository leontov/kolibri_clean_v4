#ifndef KOLIBRI_REASON_H
#define KOLIBRI_REASON_H
#include "common.h"

typedef struct {
    uint64_t step, parent, seed;
    double votes[10];
    char formula[256];
    double eff, compl;
    char prev[65];
    char hash[65];
    char hmac[65];
} ReasonBlock;

/* Build canonical JSON payload (without hash/hmac), into buf.
   Order: step,parent,seed,formula,eff,compl,prev,votes (10)
   Floats: %.17g
   No spaces anywhere.
   Example:
   {"step":1,"parent":0,"seed":123,"formula":"x","eff":0.5,"compl":4,"prev":"","votes":[0,0,...,0]}
   Returns length (>=0) or -1 on overflow. */
int rb_payload_json(const ReasonBlock* b, char* buf, size_t n);

#endif
