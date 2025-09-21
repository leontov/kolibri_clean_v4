#include "config.h"
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hex_encode(const unsigned char* in, size_t n, char* out){
    static const char* h="0123456789abcdef";
    for(size_t i=0;i<n;i++){
        out[2*i]=h[(in[i]>>4)&0xF];
        out[2*i+1]=h[in[i]&0xF];
    }
    out[2*n]=0;
}

static int write_canonical_json(const kolibri_config_t* c, char* buf, size_t n){
    return snprintf(
        buf,
        n,
        "{\"depth_decay\":%.17g,\"depth_max\":%d,\"eff_threshold\":%.17g,"
        "\"max_complexity\":%.17g,\"quorum\":%.17g,\"seed\":%llu,"
        "\"steps\":%d,\"temperature\":%.17g}",
        c->depth_decay,
        c->depth_max,
        c->eff_threshold,
        c->max_complexity,
        c->quorum,
        (unsigned long long)c->seed,
        c->steps,
        c->temperature);
}

static void refresh_fingerprint(kolibri_config_t* c){
    char canonical[sizeof(c->canonical_json)];
    int len = write_canonical_json(c, canonical, sizeof(canonical));
    if(len < 0 || (size_t)len >= sizeof(canonical)){
        c->canonical_json[0] = 0;
        c->fingerprint[0] = 0;
        return;
    }
    memcpy(c->canonical_json, canonical, (size_t)len + 1);
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)canonical, (size_t)len, digest);
    hex_encode(digest, SHA256_DIGEST_LENGTH, c->fingerprint);
}

static void json_escape(const char* in, char* out, size_t n){
    size_t j = 0;
    if(n == 0){
        return;
    }
    for(size_t i = 0; in && in[i] && j + 2 < n; ++i){
        if(in[i] == '"' || in[i] == '\\'){
            out[j++] = '\\';
        }
        out[j++] = in[i];
    }
    if(j < n){
        out[j] = 0;
    }
}

bool kolibri_load_config(kolibri_config_t* c, const char* json_path){
    c->steps = 30;
    c->depth_max = 2;
    c->depth_decay = 0.7;
    c->quorum = 0.6;
    c->temperature = 0.15;
    c->eff_threshold = 0.8;
    c->max_complexity = 32.0;
    c->seed = 987654321ULL;
    c->loaded_from_file = false;
    snprintf(c->source_path, sizeof(c->source_path), "<defaults>");
    c->canonical_json[0] = 0;
    c->fingerprint[0] = 0;

    if(!json_path){
        refresh_fingerprint(c);
        return true;
    }
    FILE* f = fopen(json_path,"rb");
    if(!f){
        refresh_fingerprint(c);
        return true;
    }
    c->loaded_from_file = true;
    strncpy(c->source_path, json_path, sizeof(c->source_path)-1);
    c->source_path[sizeof(c->source_path)-1] = 0;
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    char* buf=(char*)malloc(n+1); if(!buf){ fclose(f); return true; }
    size_t readn=fread(buf,1,(size_t)n,f);
    fclose(f);
    if(readn!=(size_t)n){
        free(buf);
        c->loaded_from_file = false;
        snprintf(c->source_path, sizeof(c->source_path), "<defaults>");
        refresh_fingerprint(c);
        return true;
    }
    buf[n]=0;
    char* p=buf;
    #define GETI(k,dst) do{ char* q=strstr(p,k); if(q){ q=strchr(q,':'); if(q){ dst=atoi(q+1);} } }while(0)
    #define GETD(k,dst) do{ char* q=strstr(p,k); if(q){ q=strchr(q,':'); if(q){ dst=atof(q+1);} } }while(0)
    #define GETU(k,dst) do{ char* q=strstr(p,k); if(q){ q=strchr(q,':'); if(q){ dst=(unsigned long long)atoll(q+1);} } }while(0)
    GETI("\"steps\"", c->steps);
    GETI("\"depth_max\"", c->depth_max);
    GETD("\"depth_decay\"", c->depth_decay);
    GETD("\"quorum\"", c->quorum);
    GETD("\"temperature\"", c->temperature);
    GETD("\"eff_threshold\"", c->eff_threshold);
    GETD("\"max_complexity\"", c->max_complexity);
    GETU("\"seed\"", c->seed);
    free(buf);
    refresh_fingerprint(c);
    return true;
}

bool kolibri_config_write_snapshot(const kolibri_config_t* cfg, const char* path){
    if(!cfg || !path || !cfg->canonical_json[0] || !cfg->fingerprint[0]){
        return false;
    }
    FILE* f = fopen(path, "wb");
    if(!f){
        return false;
    }
    char source_escaped[512];
    json_escape(cfg->source_path, source_escaped, sizeof(source_escaped));
    int written = fprintf(
        f,
        "{\n  \"source\": \"%s\",\n  \"loaded_from_file\": %s,\n  \"config\": %s,\n  \"fingerprint\": \"%s\"\n}\n",
        source_escaped,
        cfg->loaded_from_file ? "true" : "false",
        cfg->canonical_json,
        cfg->fingerprint);
    fclose(f);
    return written > 0;
}
