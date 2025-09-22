#ifndef KOLIBRI_REASON_H
#define KOLIBRI_REASON_H
#include "common.h"

typedef struct {
    uint64_t step, parent, seed;
    double votes[10];
    double vote_softmax;
    double vote_median;
    char fa[11];
    int fa_stab;
    char fa_map[32];
    double fractal_r;
    char formula[256];
    int param_count;
    double params[8];
    double bench_eff[10];
    char memory[256];
    char fmt[16];
    char origin_node[64];
    char config_fingerprint[65];
    double eff, compl;
    char prev[65];
    char merkle[65];
    char hash[65];
    char hmac[65];
} ReasonBlock;

/* Build canonical JSON payload (without hash/hmac), into buf.
   Order: step,parent,seed,config_fingerprint,fmt,formula,param_count,params,eff,compl,prev,votes (10),
          vote_softmax,vote_median,bench (10),memory,merkle,fa,fa_stab,fa_map,r
   Floats: %.17g
   No spaces anywhere.
   Example:
   {"step":1,"parent":0,"seed":123,"formula":"x","eff":0.5,"compl":4,"prev":"","votes":[0,0,...,0]}
   Returns length (>=0) or -1 on overflow. */
int rb_payload_json(const ReasonBlock* b, char* buf, size_t n);

/* Calculate a normalized benchmark validation score.
   Only finite benchmark values greater than or equal to min_eff contribute
   to the score. The result is the arithmetic mean of the accepted values.
   Non-finite values and values below the threshold are ignored. If no values
   qualify, the function returns 0. */
double rb_bench_validation_score(const ReasonBlock* b, double min_eff);

#endif
