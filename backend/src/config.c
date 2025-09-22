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

static void get_json_string_field(
    const char* json,
    const char* key,
    char* dst,
    size_t dst_size,
    bool* has_field
){
    if(!json || !key || !dst || dst_size == 0){
        return;
    }
    const char* q = strstr(json, key);
    if(!q){
        return;
    }
    q = strchr(q, ':');
    if(!q){
        return;
    }
    q++;
    while(*q == ' ' || *q == '\t'){
        q++;
    }
    if(*q != '"'){
        return;
    }
    q++;
    size_t j = 0;
    while(*q && *q != '"' && j + 1 < dst_size){
        if(*q == '\\' && q[1]){
            q++;
        }
        dst[j++] = *q++;
    }
    dst[j] = 0;
    if(has_field){
        *has_field = true;
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
    c->hmac_key[0] = 0;
    c->hmac_salt[0] = 0;
    c->has_hmac_key = false;
    c->has_hmac_salt = false;
    c->sync_enabled = false;
    c->sync_listen_port = 0;
    c->node_id[0] = 0;
    c->peer_count = 0;
    c->sync_trust_ratio = 0.5;
    for(size_t i = 0; i < KOLIBRI_MAX_PEERS; ++i){
        c->peers[i].host[0] = 0;
        c->peers[i].port = 0;
    }

    const char* env_key = getenv("KOLIBRI_HMAC_KEY");
    if(env_key && *env_key){
        strncpy(c->hmac_key, env_key, sizeof(c->hmac_key) - 1);
        c->hmac_key[sizeof(c->hmac_key) - 1] = 0;
        c->has_hmac_key = true;
    } else {
        snprintf(c->hmac_key, sizeof(c->hmac_key), "%s", "insecure-key");
    }
    const char* env_salt = getenv("KOLIBRI_HMAC_SALT");
    if(env_salt && *env_salt){
        strncpy(c->hmac_salt, env_salt, sizeof(c->hmac_salt) - 1);
        c->hmac_salt[sizeof(c->hmac_salt) - 1] = 0;
        c->has_hmac_salt = true;
    } else {
        c->hmac_salt[0] = 0;
    }

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
    get_json_string_field(p, "\"hmac_key\"", c->hmac_key, sizeof(c->hmac_key), &c->has_hmac_key);
    get_json_string_field(p, "\"hmac_salt\"", c->hmac_salt, sizeof(c->hmac_salt), &c->has_hmac_salt);
    int sync_enabled_flag = 0;
    GETI("\"sync_enabled\"", sync_enabled_flag);
    c->sync_enabled = sync_enabled_flag != 0;
    GETI("\"sync_listen_port\"", c->sync_listen_port);
    get_json_string_field(p, "\"node_id\"", c->node_id, sizeof(c->node_id), NULL);
    GETD("\"sync_trust_ratio\"", c->sync_trust_ratio);
    if(c->sync_trust_ratio < 0.0){
        c->sync_trust_ratio = 0.0;
    }
    if(c->sync_trust_ratio > 1.0){
        c->sync_trust_ratio = 1.0;
    }
    const char* peers_key = "\"sync_peers\"";
    char* peers_pos = strstr(p, peers_key);
    if(peers_pos){
        char* arr = strchr(peers_pos, '[');
        if(arr){
            arr++;
            size_t count = 0;
            while(*arr && *arr != ']' && count < KOLIBRI_MAX_PEERS){
                while(*arr == ' ' || *arr == '\n' || *arr == '\t' || *arr == ','){
                    arr++;
                }
                if(*arr != '"'){
                    break;
                }
                arr++;
                size_t j = 0;
                char entry[128] = {0};
                while(*arr && *arr != '"' && j + 1 < sizeof(entry)){
                    if(*arr == '\\' && arr[1]){
                        arr++;
                    }
                    entry[j++] = *arr++;
                }
                entry[j] = 0;
                if(*arr == '"'){
                    arr++;
                }
                while(*arr && *arr != ',' && *arr != ']'){
                    arr++;
                }
                c->peers[count].host[0] = 0;
                c->peers[count].port = 0;
                if(entry[0]){
                    char* colon = strchr(entry, ':');
                    if(colon){
                        *colon = 0;
                        colon++;
                        c->peers[count].port = atoi(colon);
                    }
                    strncpy(c->peers[count].host, entry, sizeof(c->peers[count].host) - 1);
                    c->peers[count].host[sizeof(c->peers[count].host) - 1] = 0;
                }
                count++;
                if(count >= KOLIBRI_MAX_PEERS){
                    break;
                }
                if(*arr == ','){
                    arr++;
                }
            }
            c->peer_count = count;
        }
    }
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

const char* kolibri_config_hmac_key(const kolibri_config_t* cfg){
    if(cfg && cfg->hmac_key[0]){
        return cfg->hmac_key;
    }
    const char* env_key = getenv("KOLIBRI_HMAC_KEY");
    if(env_key && *env_key){
        return env_key;
    }
    return "insecure-key";
}

size_t kolibri_config_peer_count(const kolibri_config_t* cfg){
    if(!cfg){
        return 0;
    }
    return cfg->peer_count;
}

const kolibri_peer_t* kolibri_config_peer(const kolibri_config_t* cfg, size_t index){
    if(!cfg || index >= cfg->peer_count){
        return NULL;
    }
    return &cfg->peers[index];
}
