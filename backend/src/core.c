#include "core.h"
#include "dsl.h"
#include "chainio.h"

#include "digit_agents.h"
#include "vote_aggregate.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __SSE2__
#include <immintrin.h>
#endif
#include <openssl/sha.h>
#include <openssl/hmac.h>

#define MAX_PARAMS 8
#define BENCH_COUNT 10
#define GRID_START -3.0
#define GRID_END   3.0
#define GRID_STEP  0.2

typedef struct {
    const char* name;
    double (*fn)(double);
} Benchmark;

typedef struct {
    double xs[128];
    double ys[128];
    size_t n;
} BenchData;

typedef struct {
    double eff;
    double compl;
    double params[MAX_PARAMS];
    size_t param_count;
    double bench_eff[BENCH_COUNT];
} EvalResult;

typedef struct {
    double eff;
    double compl;
    int step;
    char formula[256];
} BestEntry;

static double bench0(double x){ return sin(x)+0.5*x; }
static double bench1(double x){ return cos(x); }
static double bench2(double x){ return exp(-x*x); }
static double bench3(double x){ return x*x*x - 0.5*x; }
static double bench4(double x){ return fabs(x); }
static double bench5(double x){ return x>0.0?x:-0.5*x; }
static double bench6(double x){ return tanh(x); }
static double bench7(double x){ return 1.0/(1.0+exp(-x)); }
static double bench8(double x){ return sin(2.0*x); }
static double bench9(double x){ return log(fabs(x)+1.0); }

static const Benchmark g_benchmarks[BENCH_COUNT] = {
    {"sin+x", bench0},
    {"cos", bench1},
    {"gauss", bench2},
    {"cubic", bench3},
    {"abs", bench4},
    {"piecewise", bench5},
    {"tanh", bench6},
    {"sigmoid", bench7},
    {"sin2x", bench8},
    {"log1p", bench9}
};

static BenchData g_bench_data[BENCH_COUNT];
static bool g_bench_ready=false;
static BestEntry g_best[3];
static size_t g_best_count=0;
static char g_last_merkle[65]={0};

static void hex_encode(const unsigned char* in, size_t n, char* out){
    static const char* h="0123456789abcdef";
    for(size_t i=0;i<n;i++){ out[2*i]=h[(in[i]>>4)&0xF]; out[2*i+1]=h[in[i]&0xF]; }
    out[2*n]=0;
}

static void sha256_hex_local(const unsigned char* data, size_t n, char out[65]){
    unsigned char buf[SHA256_DIGEST_LENGTH];
    SHA256(data, n, buf);
    hex_encode(buf, SHA256_DIGEST_LENGTH, out);
}

static void ensure_bench_data(void){
    if(g_bench_ready) return;
    for(size_t bi=0; bi<BENCH_COUNT; ++bi){
        BenchData* bd=&g_bench_data[bi];
        double x=GRID_START;
        size_t idx=0;
        while(x<=GRID_END+1e-9 && idx<sizeof(bd->xs)/sizeof(bd->xs[0])){
            bd->xs[idx]=x;
            bd->ys[idx]=g_benchmarks[bi].fn(x);
            idx++;
            x+=GRID_STEP;
        }
        bd->n=idx;
    }
    g_bench_ready=true;
}

static double prng01(uint64_t* s){
    uint64_t x=*s; x^=x>>12; x^=x<<25; x^=x>>27; *s=x;
    return (x*2685821657736338717ULL)/(double)UINT64_MAX;
}

static double mse_loss(const Formula* f, const double* params, size_t param_count, const BenchData* data){
    double sum=0.0;
    size_t i=0;
#if defined(__SSE2__)
    __m128d acc = _mm_setzero_pd();
    for(; i+1<data->n; i+=2){
        double fx0=f_eval(f, params, param_count, data->xs[i]);
        double fx1=f_eval(f, params, param_count, data->xs[i+1]);
        double err0=fx0-data->ys[i];
        double err1=fx1-data->ys[i+1];
        __m128d e=_mm_set_pd(err1, err0);
        acc=_mm_add_pd(acc, _mm_mul_pd(e,e));
    }
    double buf[2];
    _mm_storeu_pd(buf, acc);
    sum += buf[0]+buf[1];
#endif
    for(; i<data->n; ++i){
        double fx=f_eval(f, params, param_count, data->xs[i]);
        double err=fx-data->ys[i];
        sum+=err*err;
    }
    return sum/(double)data->n;
}

static double mse_and_grad(const Formula* f, const double* params, size_t param_count,
                           const BenchData* data, double* grad_out){
    double loss=0.0;
    if(grad_out) memset(grad_out,0,param_count*sizeof(double));
    double tmp[MAX_PARAMS];
    for(size_t i=0;i<data->n;i++){
        memset(tmp,0,param_count*sizeof(double));
        double fx=f_eval_grad(f, params, param_count, data->xs[i], tmp);
        double err=fx-data->ys[i];
        loss+=err*err;
        if(grad_out){
            for(size_t j=0;j<param_count;j++) grad_out[j]+=2.0*err*tmp[j]/(double)data->n;
        }
    }
    return loss/(double)data->n;
}

static void init_params(double* params, size_t count){
    static const double grid[]={-2.0,-1.0,0.0,1.0,2.0};
    for(size_t i=0;i<count;i++) params[i]=grid[i%5];
}

static void project_params(double* params, size_t count){
    for(size_t i=0;i<count;i++){
        if(params[i]>5.0) params[i]=5.0;
        if(params[i]<-5.0) params[i]=-5.0;
    }
}

static void optimize_params(const Formula* f, double* params, size_t count, const BenchData* data){
    if(count==0) return;
    double m[MAX_PARAMS]={0}, v[MAX_PARAMS]={0};
    const double beta1=0.9, beta2=0.999, eps=1e-8, lr=0.05;
    double b1=1.0, b2=1.0;
    for(int iter=1; iter<=200; ++iter){
        double grad[MAX_PARAMS]={0};
        mse_and_grad(f, params, count, data, grad);
        b1*=beta1; b2*=beta2;
        for(size_t i=0;i<count;i++){
            m[i]=beta1*m[i]+(1.0-beta1)*grad[i];
            v[i]=beta2*v[i]+(1.0-beta2)*grad[i]*grad[i];
            double m_hat=m[i]/(1.0-b1);
            double v_hat=v[i]/(1.0-b2);
            params[i]-=lr*m_hat/(sqrt(v_hat)+eps);
        }
        project_params(params, count);
    }
}

static EvalResult evaluate_formula(const Formula* f){
    EvalResult r; memset(&r,0,sizeof(r));
    ensure_bench_data();
    int max_idx=f_max_param_index(f);
    if(max_idx>=0 && max_idx<MAX_PARAMS){
        r.param_count=(size_t)(max_idx+1);
        init_params(r.params, r.param_count);
        optimize_params(f, r.params, r.param_count, &g_bench_data[0]);
    }
    double mse=mse_loss(f, r.params, r.param_count, &g_bench_data[0]);
    r.eff=1.0/(1.0+mse);
    r.compl=(double)f_complexity(f);
    for(size_t i=0;i<BENCH_COUNT;i++){
        double bench_mse=mse_loss(f, r.params, r.param_count, &g_bench_data[i]);
        r.bench_eff[i]=1.0/(1.0+bench_mse);
    }
    return r;
}

static void update_memory(int step, const char* formula, double eff, double compl){
    BestEntry candidate={.eff=eff,.compl=compl,.step=step};
    snprintf(candidate.formula, sizeof(candidate.formula), "%s", formula);
    size_t pos=g_best_count<3?g_best_count:3;
    if(pos<3){ g_best[pos]=candidate; g_best_count++; }
    else g_best[2]=candidate;
    // sort descending by eff
    for(size_t i=0;i<g_best_count;i++){
        for(size_t j=i+1;j<g_best_count;j++){
            if(g_best[j].eff>g_best[i].eff){ BestEntry tmp=g_best[i]; g_best[i]=g_best[j]; g_best[j]=tmp; }
        }
    }
}

static void summarize_memory(char* out, size_t n){
    if(n==0) return;
    size_t off=0;
    int wrote = snprintf(out, n, "best[");
    if(wrote<0) return;
    off = (size_t)wrote;
    if(off>=n){
        out[n-1]=0;
        return;
    }
    for(size_t i=0;i<g_best_count;i++){
        if(off>=n) break;
        size_t remain = n-off;
        wrote=snprintf(out+off, remain,
                       "#%zu:step=%d eff=%.3f compl=%.1f %s%s",
                       i+1, g_best[i].step, g_best[i].eff, g_best[i].compl,
                       g_best[i].formula, (i+1<g_best_count)?"; ":"");
        if(wrote<0) break;
        if((size_t)wrote >= remain){
            off=n;
            break;
        }
        off += (size_t)wrote;
    }
    if(off<n){
        size_t remain = n-off;
        wrote = snprintf(out+off, remain, "]");
        if(wrote<0 && n>0) out[n-1]=0;
    } else if(n>0){
        out[n-1]=0;
    }
}

static double vote_softmax_avg(const double votes[10], double temperature){
    double temp = (temperature>1e-3)?temperature:1e-3;
    double maxv=votes[0];
    for(int i=1;i<10;i++) if(votes[i]>maxv) maxv=votes[i];
    double num=0.0, den=0.0;
    for(int i=0;i<10;i++){
        double w=exp((votes[i]-maxv)/temp);
        num+=votes[i]*w;
        den+=w;
    }
    return (den>0.0)?(num/den):0.0;
}

static int cmp_pair(const void* a, const void* b){
    const double* da=(const double*)a;
    const double* db=(const double*)b;
    if(da[0]<db[0]) return -1;
    if(da[0]>db[0]) return 1;
    return 0;
}

static double vote_weighted_median(const double votes[10]){
    double pairs[10][2];
    double total=0.0;
    for(int i=0;i<10;i++){
        double w=votes[i];
        if(w<0.0) w=0.0;
        pairs[i][0]=votes[i];
        pairs[i][1]=w;
        total+=w;
    }
    if(total<=1e-9) return 0.0;
    qsort(pairs, 10, sizeof(pairs[0]), cmp_pair);
    double acc=0.0;
    for(int i=0;i<10;i++){
        acc+=pairs[i][1];
        if(acc>=total*0.5) return pairs[i][0];
    }
    return pairs[9][0];
}

static void compute_merkle(const char* prev_merkle, ReasonBlock* b){
    static const char zeros[65]="0000000000000000000000000000000000000000000000000000000000000000";
    const char* prev = (prev_merkle && prev_merkle[0])?prev_merkle:zeros;
    b->merkle[0]=0;
    char payload[4096];
    int len = rb_payload_json(b, payload, sizeof(payload));
    if(len<0) return;
    size_t prev_len=strlen(prev);
    size_t total=prev_len + (size_t)len;
    unsigned char* buf=(unsigned char*)malloc(total);
    memcpy(buf, prev, prev_len);
    memcpy(buf+prev_len, payload, (size_t)len);
    sha256_hex_local(buf, total, b->merkle);
    free(buf);
}

static DigitField g_digit_field;
static bool g_digit_field_ready = false;

static void digit_field_shutdown(void) {
    if (g_digit_field_ready) {
        digit_field_free(&g_digit_field);
        g_digit_field_ready = false;
    }
}

static bool ensure_digit_field(const kolibri_config_t* cfg) {
    if (g_digit_field_ready) {
        return true;
    }
    if (!digit_field_init(&g_digit_field, cfg->depth_max > 0 ? cfg->depth_max : 1, cfg->seed)) {
        return false;
    }
    atexit(digit_field_shutdown);
    g_digit_field_ready = true;
    return true;
}

static Formula* propose_formula(const VoteState* state, uint64_t seed){
    uint64_t s=seed;
    int choice=(int)floor(prng01(&s)*6.0);
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
    ensure_bench_data();
    memset(out,0,sizeof(*out));
    out->step=step; out->parent=(step>0)?(step-1):0; out->seed=cfg->seed^((uint64_t)step);
    if(cfg->fingerprint[0]){
        snprintf(out->config_fingerprint, sizeof(out->config_fingerprint), "%s", cfg->fingerprint);
    }
    strncpy(out->fmt, "dsl-v1", sizeof(out->fmt)-1);


    if (!ensure_digit_field(cfg)) {
        return false;
    }

    digit_tick(&g_digit_field);

    VoteState vote_state;
    memset(&vote_state, 0, sizeof(vote_state));
    digit_aggregate(&g_digit_field, &vote_state);
    vote_state.temperature = cfg->temperature;

    VotePolicy policy = vote_policy_from_config(cfg);
    vote_apply_policy(&vote_state, &policy);

    for (int i = 0; i < 10; ++i) {
        out->votes[i] = vote_state.vote[i];
    }
    out->vote_softmax = vote_softmax_avg(out->votes, cfg->temperature);
    out->vote_median = vote_weighted_median(out->votes);

    Formula* f = propose_formula(&vote_state, out->seed);
    EvalResult er = evaluate_formula(f);
    out->eff = er.eff;
    out->compl = er.compl;
    out->param_count = (int)er.param_count;
    for (size_t i = 0; i < er.param_count; ++i) {
        out->params[i] = er.params[i];
    }
    for (size_t i = 0; i < BENCH_COUNT; ++i) {
        out->bench_eff[i] = er.bench_eff[i];
    }

    f_render(f, out->formula, sizeof(out->formula));

    update_memory(step, out->formula, out->eff, out->compl);
    summarize_memory(out->memory, sizeof(out->memory));

    if(prev_hash && *prev_hash) snprintf(out->prev, sizeof(out->prev), "%s", prev_hash);

    compute_merkle(g_last_merkle, out);

    char payload[4096];
    int len = rb_payload_json(out, payload, sizeof(payload));
    if(len<0){ f_free(f); return false; }

    char hash_hex[65];
    sha256_hex_local((unsigned char*)payload, (size_t)len, hash_hex);
    const char* key = getenv("KOLIBRI_HMAC_KEY");
    if(!key) key = "insecure-key";
    unsigned char* mac = HMAC(EVP_sha256(), key, (int)strlen(key),
                              (unsigned char*)payload, (size_t)len, NULL, NULL);
    char hmac_hex[65];
    hex_encode(mac, 32, hmac_hex);
    snprintf(out->hash, sizeof(out->hash), "%s", hash_hex);
    snprintf(out->hmac, sizeof(out->hmac), "%s", hmac_hex);
    if(out_hash) snprintf(out_hash, 65, "%s", hash_hex);

    if(!chain_append("logs/chain.jsonl", out)){ f_free(f); return false; }

    snprintf(g_last_merkle, sizeof(g_last_merkle), "%s", out->merkle);

    f_free(f);
    return true;
}
