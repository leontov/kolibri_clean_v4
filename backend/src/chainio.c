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

bool chain_append(const char* path, const ReasonBlock* b){
    FILE* f = fopen(path, "ab"); if(!f) return false;
    char payload[4096]; int m = rb_payload_json(b, payload, sizeof(payload));
    if(m<0){ fclose(f); return false; }

    char keybuf[256]; const char* k = getenv("KOLIBRI_HMAC_KEY");
    if(!k){ k = "insecure-key"; }
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
        sscanf(line, "{\"step\":%llu", &b->step);
        char* p=strstr(line,"\"parent\":"); if(p) sscanf(p,"\"parent\":%llu",&b->parent);
        p=strstr(line,"\"seed\":"); if(p) sscanf(p,"\"seed\":%llu",&b->seed);
        p=strstr(line,"\"eff\":"); if(p) sscanf(p,"\"eff\":%lf",&b->eff);
        p=strstr(line,"\"compl\":"); if(p) sscanf(p,"\"compl\":%lf",&b->compl);
        p=strstr(line,"\"prev\":\""); if(p) sscanf(p,"\"prev\":\"%64[^\"]\"", b->prev);
        p=strstr(line,"\"hash\":\""); if(p) sscanf(p,"\"hash\":\"%64[0-9a-f]\"", b->hash);
        p=strstr(line,"\"hmac\":\""); if(p) sscanf(p,"\"hmac\":\"%64[0-9a-f]\"", b->hmac);
        p=strstr(line,"\"formula\":\"");
        if(p){
            p+=11; size_t j=0;
            while(*p && *p!='"' && j<sizeof(b->formula)-1){
                if(*p=='\\' && p[1]) p++; b->formula[j++]=*p++;
            }
            b->formula[j]=0;
        }
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
        n++;
    }
    fclose(f);
    *out_arr=arr; *out_n=n; return true;
}
