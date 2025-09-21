#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "chainio.h"
#include "config.h"
#include "reason.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static void make_temp_path(char* buf, size_t n){
#ifdef _WIN32
    if(_tmpnam_s(buf, n) != 0){
        fprintf(stderr, "_tmpnam_s failed\n");
        exit(1);
    }
#else
    char tmpl[] = "/tmp/kolibri_verify_XXXXXX";
    if(n < sizeof(tmpl)){
        fprintf(stderr, "buffer too small for temp path\n");
        exit(1);
    }
    int fd = mkstemp(tmpl);
    if(fd < 0){
        perror("mkstemp");
        exit(1);
    }
    close(fd);
    if(remove(tmpl) != 0){
        perror("remove");
        exit(1);
    }
    strncpy(buf, tmpl, n - 1);
    buf[n - 1] = 0;
#endif
}

static void init_block(ReasonBlock* b){
    memset(b, 0, sizeof(*b));
    b->step = 1;
    b->parent = 0;
    b->seed = 12345;
    snprintf(b->config_fingerprint, sizeof(b->config_fingerprint), "test-fp");
    snprintf(b->fmt, sizeof(b->fmt), "test");
    snprintf(b->formula, sizeof(b->formula), "x+1");
    b->eff = 0.5;
    b->compl = 1.0;
}

int main(void){
    char path[256];
    make_temp_path(path, sizeof(path));

    ReasonBlock block;
    init_block(&block);

    kolibri_config_t good_cfg;
    kolibri_load_config(&good_cfg, NULL);
    snprintf(good_cfg.hmac_key, sizeof(good_cfg.hmac_key), "verify-key-%ld", (long)getpid());
    good_cfg.has_hmac_key = true;

    if(!chain_append(path, &block, &good_cfg)){
        fprintf(stderr, "chain_append failed\n");
        remove(path);
        return 1;
    }

    if(!chain_verify(path, NULL, &good_cfg)){
        fprintf(stderr, "expected verification success with matching config\n");
        remove(path);
        return 1;
    }

    kolibri_config_t bad_cfg = good_cfg;
    snprintf(bad_cfg.hmac_key, sizeof(bad_cfg.hmac_key), "verify-key-bad-%ld", (long)getpid());
    bad_cfg.has_hmac_key = true;

    if(chain_verify(path, NULL, &bad_cfg)){
        fprintf(stderr, "expected verification failure with mismatched key\n");
        remove(path);
        return 1;
    }

    remove(path);
    return 0;
}
