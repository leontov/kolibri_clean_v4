#include "core.h"
#include "fmt_v5.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    printf("Kolibri CLI\n");
    printf("Usage:\n");
    printf("  kolibri run [--config path] [--steps N] [--beam B] [--lambda L] [--fmt F] [--output file] [--selfcheck]\n");
    printf("  kolibri verify <file>\n");
    printf("  kolibri replay <file>\n");
}

int main(int argc, char **argv) {
    fmt_init_locale();
    if (argc < 2) {
        print_usage();
        return 1;
    }
    const char *command = argv[1];
    if (strcmp(command, "run") == 0) {
        const char *config_path = "configs/kolibri.json";
        size_t steps = 0;
        int beam = 0;
        double lambda = 0.0;
        int fmt_id = 0;
        const char *output_path = NULL;
        int selfcheck = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
                config_path = argv[++i];
            } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
                steps = (size_t)strtoull(argv[++i], NULL, 10);
            } else if (strcmp(argv[i], "--beam") == 0 && i + 1 < argc) {
                beam = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--lambda") == 0 && i + 1 < argc) {
                lambda = strtod(argv[++i], NULL);
            } else if (strcmp(argv[i], "--fmt") == 0 && i + 1 < argc) {
                fmt_id = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                output_path = argv[++i];
            } else if (strcmp(argv[i], "--selfcheck") == 0) {
                selfcheck = 1;
            } else {
                printf("Unknown option: %s\n", argv[i]);
                return 1;
            }
        }
        KolibriConfig cfg;
        char cfg_hash[KOLIBRI_HASH_TEXT];
        char err[128];
        if (core_load_config(config_path, &cfg, cfg_hash, err, sizeof(err)) != 0) {
            printf("Error: %s\n", err);
            return 1;
        }
        if (core_run(&cfg, cfg_hash, steps, beam, lambda, fmt_id, output_path) != 0) {
            printf("Error: failed to run pipeline\n");
            return 1;
        }
        const char *out_path = output_path && output_path[0] ? output_path : cfg.output_path;
        size_t actual_steps = steps ? steps : cfg.steps_default;
        printf("OK: wrote %s (%zu blocks)\n", out_path, actual_steps);
        if (selfcheck) {
            const char *hmac_key_env = getenv("KOLIBRI_HMAC_KEY");
            size_t blocks = 0;
            size_t failed = 0;
            if (core_verify_chain(out_path, hmac_key_env, &blocks, &failed, err, sizeof(err)) != 0) {
                printf("Verify failed at step %zu: %s\n", failed, err);
                return 1;
            }
            printf("OK: chain verified (%zu blocks)\n", blocks);
        }
        return 0;
    } else if (strcmp(command, "verify") == 0) {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        const char *path = argv[2];
        const char *hmac_key_env = getenv("KOLIBRI_HMAC_KEY");
        size_t blocks = 0;
        size_t failed = 0;
        char err[128];
        if (core_verify_chain(path, hmac_key_env, &blocks, &failed, err, sizeof(err)) != 0) {
            printf("FAIL: %s\n", err);
            return 1;
        }
        printf("OK: chain verified (%zu blocks)\n", blocks);
        return 0;
    } else if (strcmp(command, "replay") == 0) {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        const char *path = argv[2];
        char hash[KOLIBRI_HASH_TEXT];
        char err[128];
        if (core_replay_chain(path, hash, sizeof(hash), err, sizeof(err)) != 0) {
            printf("Error: %s\n", err);
            return 1;
        }
        printf("Final hash: %s\n", hash);
        return 0;
    } else if (strcmp(command, "--help") == 0 || strcmp(command, "help") == 0) {
        print_usage();
        return 0;
    }
    print_usage();
    return 1;
}
