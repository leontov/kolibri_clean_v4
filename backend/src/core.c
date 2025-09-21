#include "core.h"
#include "dsl.h"
#include "chainio.h"
#include "digit_agents.h"
#include "vote_aggregate.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static double prng01(uint64_t* s){
    uint64_t x=*s; x^=x>>12; x^=x<<25; x^=x>>27; *s=x;
    return (x*2685821657736338717ULL)/(double)UINT64_MAX;
}

static double target(double x){ return sin(x)+0.5*x; }

static double eff_of(const Formula* f){
    double mse=0.0; int n=0;
    for(double x=-3.0;x<=3.0;x+=0.2){
        double e=f_eval(f,x)-target(x); mse+=e*e; n++;
    }
    mse/=n; return 1.0/(1.0+mse);
}

static DigitField g_field;
static bool g_field_ready = false;

static void cleanup_digit_field(void) {
    if (g_field_ready) {
        digit_field_free(&g_field);
        g_field_ready = false;
    }
}

static Formula* propose_formula(const VoteState* state, uint64_t seed){
    uint64_t s=seed;
    int choice=(int)floor(prng01(&s)*5.0);
    switch(choice){
        case 0:
            return f_add(f_x(), f_sin(f_x()));
        case 1:
            return f_sin(f_mul(f_const(0.1 + 2.9*state->vote[2]), f_x()));
        case 2:
            return f_add(f_mul(f_const(-2+4*state->vote[0]), f_sin(f_x())),
                         f_mul(f_const(-1+2*state->vote[1]), f_x()));
        case 3:
            return f_const(-1+2*state->vote[3]);
        default:
            return f_const(-2+4*state->vote[4]);
    }
}

bool kolibri_step(const kolibri_config_t* cfg, int step, const char* prev_hash,
                  ReasonBlock* out, char out_hash[65]){
    memset(out,0,sizeof(*out));
    out->step=step; out->parent=step-1; out->seed=cfg->seed^((uint64_t)step);

    if(!g_field_ready){
        if(!digit_field_init(&g_field, cfg->depth_max, cfg->seed)){
            return false;
        }
        g_field_ready = true;
        atexit(cleanup_digit_field);
    }

    digit_tick(&g_field, NULL);

    VoteState vote_state;
    memset(&vote_state, 0, sizeof(vote_state));
    digit_aggregate(&g_field, &vote_state);
    vote_state.temperature = cfg->temperature;

    VotePolicy policy = { cfg->depth_decay, cfg->quorum };
    vote_apply_policy(&vote_state, &policy);

    for (int i = 0; i < 10; ++i) {
        out->votes[i] = vote_state.vote[i];
    }

    Formula* f = propose_formula(&vote_state, out->seed);
    out->eff = eff_of(f);
    out->compl = (double)f_complexity(f);
    f_render(f, out->formula, sizeof(out->formula));
    f_free(f);

    if(prev_hash && *prev_hash) strncpy(out->prev, prev_hash, 65);

    // append to chain (hash/hmac computed over canonical JSON payload)
    if(!chain_append("logs/chain.jsonl", out)) return false;

    // compute the same payload to return its hash
    char payload[4096]; int m = rb_payload_json(out, payload, sizeof(payload));
    if(m>0){
        // hash in out_hash is for convenience upstream
        extern void __sha256_hex(const unsigned char*, size_t, char*);
    }
    // simple: reload last line for hash
    ReasonBlock* arr=NULL; size_t n=0;
    if(chain_load("logs/chain.jsonl",&arr,&n) && n>0){
        strncpy(out_hash, arr[n-1].hash, 65);
        free(arr);
        return true;
    }
    return false;
}
