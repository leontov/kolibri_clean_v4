#include "chainio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>

static void hex(const unsigned char* in, size_t n, char* out){
    static const char* h="0123456789abcdef";
    for(size_t i=0;i<n;i++){ out[2*i]=h[(in[i]>>4)&0xF]; out[2*i+1]=h[in[i]&0xF]; }
    out[2*n]=0;
}

static void sha256_hex(const unsigned char* d, size_t n, char out[65]){
    unsigned char buf[SHA256_DIGEST_LENGTH];
    SHA256(d,n,buf); hex(buf, SHA256_DIGEST_LENGTH, out);
}

bool chain_append(const char* path, const ReasonBlock* b, const kolibri_config_t* cfg){
    FILE* f = fopen(path, "ab"); if(!f) return false;
    char payload[4096]; int m = rb_payload_json(b, payload, sizeof(payload));
    if(m<0){ fclose(f); return false; }

    const char* k = kolibri_config_hmac_key(cfg);
    // hash/hmac over EXACT payload bytes
    char hash[65], hmac_hex[65];
    sha256_hex((unsigned char*)payload, (size_t)m, hash);
    unsigned char* mac = HMAC(EVP_sha256(), k, (int)strlen(k),
                              (unsigned char*)payload, (size_t)m, NULL, NULL);
    if(!mac){ fclose(f); return false; }
    hex(mac, 32, hmac_hex);

    // write: payload + ,"hash":"..","hmac":".."\n
    fprintf(f, "%.*s,\"hash\":\"%s\",\"hmac\":\"%s\"}\n", m-1, payload, hash, hmac_hex);
    fclose(f);
    return true;
}

bool chain_load(const char* path, ReasonBlock** out_arr, size_t* out_n){
    *out_arr=NULL; *out_n=0;
    FILE* f=fopen(path,"rb"); if(!f) return false;
    size_t cap=128, n=0; ReasonBlock* arr=(ReasonBlock*)calloc(cap,sizeof(ReasonBlock));
    char line[8192];
    while(fgets(line,sizeof(line),f)){
        if(n==cap){ cap*=2; arr=(ReasonBlock*)realloc(arr,cap*sizeof(ReasonBlock)); }
        ReasonBlock* b=&arr[n]; memset(b,0,sizeof(*b));
        // naive parse
        unsigned long long tmp=0ULL;
        if(sscanf(line, "{\"step\":%llu", &tmp)==1) b->step=(uint64_t)tmp;
        char* p=strstr(line,"\"parent\":"); if(p && sscanf(p,"\"parent\":%llu",&tmp)==1) b->parent=(uint64_t)tmp;
        p=strstr(line,"\"seed\":"); if(p && sscanf(p,"\"seed\":%llu",&tmp)==1) b->seed=(uint64_t)tmp;
        p=strstr(line,"\"config_fingerprint\":\"");
        if(p){
            p+=strlen("\"config_fingerprint\":\"");
            size_t j=0;
            while(*p && *p!='"' && j<sizeof(b->config_fingerprint)-1){
                if(*p=='\\' && p[1]) p++;
                b->config_fingerprint[j++]=*p++;
            }
            b->config_fingerprint[j]=0;
        }
        p=strstr(line,"\"fmt\":\"");
        if(p){ p+=strlen("\"fmt\":\""); size_t j=0; while(*p && *p!='"' && j<sizeof(b->fmt)-1){ if(*p=='\\'&&p[1]) p++; b->fmt[j++]=*p++; } b->fmt[j]=0; }
        p=strstr(line,"\"formula\":\"");
        if(p){
            p+=strlen("\"formula\":\""); size_t j=0;
            while(*p && *p!='"' && j<sizeof(b->formula)-1){
                if(*p=='\\' && p[1]) p++;
                b->formula[j++]=*p++;
            }
            b->formula[j]=0;
        }
        p=strstr(line,"\"param_count\":"); if(p) sscanf(p,"\"param_count\":%d", &b->param_count);
        if(b->param_count<0) b->param_count=0;
        if(b->param_count>8) b->param_count=8;
        p=strstr(line,"\"params\":[");
        if(p){
            p=strchr(p,'['); if(p) p++;
            for(int i=0;i<b->param_count;i++){
                while(*p==' '||*p=='\t') p++;
                b->params[i]=strtod(p,&p);
                while(*p && *p!=',' && *p!=']') p++;
                if(*p==',') p++;
            }
        }
        p=strstr(line,"\"eff\":"); if(p) sscanf(p,"\"eff\":%lf",&b->eff);
        p=strstr(line,"\"compl\":"); if(p) sscanf(p,"\"compl\":%lf",&b->compl);
        p=strstr(line,"\"prev\":\"");
        if(p){ p+=strlen("\"prev\":\""); size_t j=0; while(*p && *p!='"' && j<sizeof(b->prev)-1){ if(*p=='\\'&&p[1]) p++; b->prev[j++]=*p++; } b->prev[j]=0; }
        p=strstr(line,"\"hash\":\""); if(p) sscanf(p,"\"hash\":\"%64[0-9a-f]\"", b->hash);
        p=strstr(line,"\"hmac\":\""); if(p) sscanf(p,"\"hmac\":\"%64[0-9a-f]\"", b->hmac);
        p=strstr(line,"\"votes\":[");
        if(p){
            p=strchr(p,'['); if(p) p++;
            for(int i=0;i<10;i++){
                while(*p==' '||*p=='\t') p++;
                char* e=NULL; double v=strtod(p,&e);
                if(e==p){ for(;i<10;i++) b->votes[i]=0.0; break; }
                b->votes[i]=v; p=e;
                while(*p && *p!=',' && *p!=']') p++;
                if(*p==',') p++;
                if(*p==']'){ for(int k=i+1;k<10;k++) b->votes[k]=0.0; break; }
            }
        }
        p=strstr(line,"\"vote_softmax\":"); if(p) sscanf(p,"\"vote_softmax\":%lf", &b->vote_softmax);
        p=strstr(line,"\"vote_median\":"); if(p) sscanf(p,"\"vote_median\":%lf", &b->vote_median);
        p=strstr(line,"\"bench\":[");
        if(p){
            p=strchr(p,'['); if(p) p++;
            for(int i=0;i<10;i++){
                while(*p==' '||*p=='\t') p++;
                char* e=NULL; double v=strtod(p,&e);
                if(e==p){ for(;i<10;i++) b->bench_eff[i]=0.0; break; }
                b->bench_eff[i]=v; p=e;
                while(*p && *p!=',' && *p!=']') p++;
                if(*p==',') p++;
                if(*p==']'){ for(int k=i+1;k<10;k++) b->bench_eff[k]=0.0; break; }
            }
        }
        p=strstr(line,"\"memory\":\"");
        if(p){ p+=strlen("\"memory\":\""); size_t j=0; while(*p && *p!='"' && j<sizeof(b->memory)-1){ if(*p=='\\'&&p[1]) p++; b->memory[j++]=*p++; } b->memory[j]=0; }
        p=strstr(line,"\"merkle\":\"");
        if(p) sscanf(p,"\"merkle\":\"%64[0-9a-f]\"", b->merkle);
        p=strstr(line,"\"fa\":\"");
        if(p){ p+=strlen("\"fa\":\""); size_t j=0; while(*p && *p!='"' && j<sizeof(b->fa)-1){ if(*p=='\\'&&p[1]) p++; b->fa[j++]=*p++; } b->fa[j]=0; }
        p=strstr(line,"\"fa_stab\":"); if(p) sscanf(p,"\"fa_stab\":%d", &b->fa_stab);
        p=strstr(line,"\"fa_map\":\"");
        if(p){ p+=strlen("\"fa_map\":\""); size_t j=0; while(*p && *p!='"' && j<sizeof(b->fa_map)-1){ if(*p=='\\'&&p[1]) p++; b->fa_map[j++]=*p++; } b->fa_map[j]=0; }
        p=strstr(line,",\"r\":");
        if(!p){ p=strstr(line,"\"r\":"); }
        if(p) sscanf(p,"%*[^0-9-]%lf", &b->fractal_r);
        n++;
    }
    fclose(f);
    *out_arr=arr; *out_n=n; return true;
}

bool chain_verify(const char* path, FILE* out, const kolibri_config_t* cfg){
    ReasonBlock* arr=NULL; size_t n=0;
    if(!chain_load(path,&arr,&n) || n==0){
        if(out) fprintf(out,"No chain at %s\n", path);
        free(arr);
        return false;
    }
    const char* k = kolibri_config_hmac_key(cfg);

    char prev[65]={0};
    for(size_t i=0;i<n;i++){
        ReasonBlock* b = &arr[i];
        if(strcmp(b->prev, prev)!=0){
            if(out) fprintf(out, "prev mismatch at step %llu\n", (unsigned long long)b->step);
            free(arr); return false;
        }
        char payload[4096]; int m = rb_payload_json(b, payload, sizeof(payload));
        if(m<0){ free(arr); return false; }
        char h[65], mac_hex[65];
        sha256_hex((unsigned char*)payload, (size_t)m, h);
        if(strncmp(h, b->hash, 64)!=0){
            if(out) fprintf(out, "hash mismatch at step %llu\n", (unsigned long long)b->step);
            free(arr); return false;
        }
        unsigned char* mac = HMAC(EVP_sha256(), k, (int)strlen(k),
                                  (unsigned char*)payload, (size_t)m, NULL, NULL);
        hex(mac, 32, mac_hex);
        if(strncmp(mac_hex, b->hmac, 64)!=0){
            if(out) fprintf(out, "hmac mismatch at step %llu\n", (unsigned long long)b->step);
            free(arr); return false;
        }
        strncpy(prev, h, 65);
    }
    if(out) fprintf(out, "OK: chain verified (%zu blocks)\n", n);
    free(arr);
    return true;
}
