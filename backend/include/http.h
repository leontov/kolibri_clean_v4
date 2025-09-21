#ifndef KOLIBRI_HTTP_H
#define KOLIBRI_HTTP_H

#include "core.h"

typedef struct {
    int port;
    int cors_dev;
    char static_root[512];
} kolibri_http_config_t;

int kolibri_http_serve(kolibri_runtime_t *rt,
                       kolibri_chain_t *chain,
                       const kolibri_http_config_t *config);

#endif
