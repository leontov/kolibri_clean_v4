#include "reason.h"
#include <string.h>
#include <stdio.h>

static void esc(const char* s, char* out, size_t n){
    size_t j=0; if(n) out[0]=0;
    for(size_t i=0; s && s[i] && j+2<n; i++){
        if(s[i]=='"'||s[i]=='\\'){ out[j++]='\\'; out[j++]=s[i]; }
        else out[j++]=s[i];
    }
    if(j<n) out[j]=0;
}

int rb_payload_json(const ReasonBlock* b, char* buf, size_t n){
    // deterministic JSON: no spaces; floats as %.17g; exactly 10 votes
    char fesc[512]; esc(b->formula, fesc, sizeof(fesc));
    char memesc[512]; esc(b->memory, memesc, sizeof(memesc));
    char fmtesc[64]; esc(b->fmt, fmtesc, sizeof(fmtesc));
    char prev_esc[128]; esc(b->prev, prev_esc, sizeof(prev_esc));
    char merkle_esc[128]; esc(b->merkle, merkle_esc, sizeof(merkle_esc));
    int off=0;
    off += snprintf(buf+off, n-off, "{\"step\":%llu,\"parent\":%llu,\"seed\":%llu,",
                    (unsigned long long)b->step,
                    (unsigned long long)b->parent,
                    (unsigned long long)b->seed);
    off += snprintf(buf+off, n-off, "\"fmt\":\"%s\",\"formula\":\"%s\",", fmtesc, fesc);
    off += snprintf(buf+off, n-off, "\"param_count\":%d,\"params\":[", b->param_count);
    for(int i=0;i<b->param_count;i++){
        off += snprintf(buf+off, n-off, "%.17g%s", b->params[i], (i<b->param_count-1)?",":"");
    }
    off += snprintf(buf+off, n-off, "]");
    off += snprintf(buf+off, n-off, ",\"eff\":%.17g,\"compl\":%.17g,",
                    b->eff, b->compl);
    off += snprintf(buf+off, n-off, "\"prev\":\"%s\",\"votes\":[", prev_esc);
    for(int i=0;i<10;i++){
        off += snprintf(buf+off, n-off, "%.17g%s", b->votes[i], (i<9)?",":"]");
    }

    off += snprintf(buf+off, n-off, ",\"vote_softmax\":%.17g,\"vote_median\":%.17g,",
                    b->vote_softmax, b->vote_median);
    off += snprintf(buf+off, n-off, "\"bench\":[");
    for(int i=0;i<10;i++){
        off += snprintf(buf+off, n-off, "%.17g%s", b->bench_eff[i], (i<9)?",":"]");
    }
    off += snprintf(buf+off, n-off, ",\"memory\":\"%s\",\"merkle\":\"%s\"}", memesc, merkle_esc);

    if(off<0 || (size_t)off>=n) return -1;
    return off;
}
