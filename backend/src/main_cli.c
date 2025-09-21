#include "core.h"
#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fprintf(stderr, "kolibri <run|verify|replay|serve> [options]\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    const char *command = argv[1];
    if (strcmp(command, "run") == 0) {
        const char *config_path = "configs/kolibri.json";
        unsigned steps = 30;
        unsigned beam = 1;
        double lambda = 0.0;
        unsigned fmt = 5;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
                config_path = argv[++i];
            } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
                steps = (unsigned)strtoul(argv[++i], NULL, 10);
            } else if (strcmp(argv[i], "--beam") == 0 && i + 1 < argc) {
                beam = (unsigned)strtoul(argv[++i], NULL, 10);
            } else if (strcmp(argv[i], "--lambda") == 0 && i + 1 < argc) {
                lambda = strtod(argv[++i], NULL);
            } else if (strcmp(argv[i], "--fmt") == 0 && i + 1 < argc) {
                fmt = (unsigned)strtoul(argv[++i], NULL, 10);
            }
        }
        kolibri_runtime_t runtime;
        if (kolibri_runtime_init(&runtime, config_path) != 0) {
            fprintf(stderr, "failed to init runtime\n");
            return 1;
        }
        kolibri_chain_t chain;
        kolibri_chain_init(&chain);
        if (kolibri_run_with_callback(&runtime, steps, beam, lambda, fmt, KOLIBRI_DEFAULT_LOG, &chain, NULL, NULL) != 0) {
            fprintf(stderr, "run failed\n");
            kolibri_chain_free(&chain);
            kolibri_runtime_free(&runtime);
            return 1;
        }
        kolibri_chain_free(&chain);
        kolibri_runtime_free(&runtime);
        printf("run complete: %u blocks -> %s\n", steps, KOLIBRI_DEFAULT_LOG);
        return 0;
    }
    if (strcmp(command, "verify") == 0) {
        const char *config_path = "configs/kolibri.json";
        const char *path = KOLIBRI_DEFAULT_LOG;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
                config_path = argv[++i];
            } else {
                path = argv[i];
            }
        }
        kolibri_config_t cfg;
        if (kolibri_load_config(config_path, &cfg) != 0) {
            fprintf(stderr, "failed to load config %s\n", config_path);
            return 1;
        }
        if (kolibri_verify_file(path, &cfg, 1) != 0) {
            return 1;
        }
        return 0;
    }
    if (strcmp(command, "replay") == 0) {
        const char *config_path = "configs/kolibri.json";
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
                config_path = argv[++i];
            }
        }
        kolibri_runtime_t runtime;
        if (kolibri_runtime_init(&runtime, config_path) != 0) {
            fprintf(stderr, "failed to init runtime\n");
            return 1;
        }
        kolibri_replay(&runtime.config);
        kolibri_runtime_free(&runtime);
        return 0;
    }
    if (strcmp(command, "serve") == 0) {
        const char *config_path = "configs/kolibri.json";
        const char *static_root = "web/dist";
        int port = 8080;
        int cors_dev = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
                config_path = argv[++i];
            } else if (strcmp(argv[i], "--static") == 0 && i + 1 < argc) {
                static_root = argv[++i];
            } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
                port = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--cors-dev") == 0) {
                cors_dev = 1;
            }
        }
        kolibri_runtime_t runtime;
        if (kolibri_runtime_init(&runtime, config_path) != 0) {
            fprintf(stderr, "failed to init runtime\n");
            return 1;
        }
        kolibri_chain_t chain;
        kolibri_chain_init(&chain);
        kolibri_http_config_t http_cfg;
        http_cfg.port = port;
        http_cfg.cors_dev = cors_dev;
        strncpy(http_cfg.static_root, static_root, sizeof(http_cfg.static_root) - 1);
        http_cfg.static_root[sizeof(http_cfg.static_root) - 1] = '\0';
        int rc = kolibri_http_serve(&runtime, &chain, &http_cfg);
        kolibri_chain_free(&chain);
        kolibri_runtime_free(&runtime);
        return rc;
    }
    usage();
    return 1;
}
