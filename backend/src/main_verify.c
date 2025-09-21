#include "chainio.h"
#include "reason.h"
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

int main(int argc, char** argv){
    const char* path = (argc>1)? argv[1] : "logs/chain.jsonl";
    ReasonBlock* arr=NULL; size_t n=0;
    if(!chain_load(path,&arr,&n) || n==0){
        fprintf(stderr,"No chain at %s\n", path);
        return 1;
    }
    const char* k = getenv("KOLIBRI_HMAC_KEY");
    if(!k) k = "insecure-key";

    char prev[65]={0};
    for(size_t i=0;i<n;i++){
        ReasonBlock* b = &arr[i];
        if(strcmp(b->prev, prev)!=0){
            fprintf(stderr, "prev mismatch at step %llu\n", (unsigned long long)b->step);
            free(arr); return 2;
        }
        char payload[4096]; int m = rb_payload_json(b, payload, sizeof(payload));
        if(m<0){ free(arr); return 3; }
        char h[65], mac_hex[65];
        sha256_hex((unsigned char*)payload, (size_t)m, h);
        if(strncmp(h, b->hash, 64)!=0){
            fprintf(stderr, "hash mismatch at step %llu\n", (unsigned long long)b->step);
            free(arr); return 4;
        }
        unsigned char* mac = HMAC(EVP_sha256(), k, (int)strlen(k),
                                  (unsigned char*)payload, (size_t)m, NULL, NULL);
        hex(mac, 32, mac_hex);
        if(strncmp(mac_hex, b->hmac, 64)!=0){
            fprintf(stderr, "hmac mismatch at step %llu\n", (unsigned long long)b->step);
            free(arr); return 5;
        }
        strncpy(prev, h, 65);
    }
    printf("OK: chain verified (%zu blocks)\n", n);
    free(arr);
    return 0;
}
