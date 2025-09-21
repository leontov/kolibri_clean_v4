#include "core.h"

#include "fmt_v5.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int kolibri_verify_file(const char *path, int verbose) {
    fmt_init_locale();
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "verify: unable to open %s\n", path);
        return -1;
    }
    kolibri_config_t cfg;
    if (kolibri_load_config("configs/kolibri.json", &cfg) != 0) {
        memset(&cfg, 0, sizeof(cfg));
    }
    char line[8192];
    char prev_hash[65] = "";
    unsigned expected_step = 0;
    size_t blocks = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        kolibri_payload_t payload;
        char hash_hex[65];
        char hmac_hex[65];
        if (kolibri_parse_block_line(line, len, &payload, hash_hex, hmac_hex) != 0) {
            fclose(fp);
            fprintf(stderr, "verify: invalid block format\n");
            return -1;
        }
        char computed_hash[65];
        kolibri_hash_payload(&payload, computed_hash);
        if (strcmp(computed_hash, hash_hex) != 0) {
            fclose(fp);
            fprintf(stderr, "verify: hash mismatch at step %u\n", payload.step);
            free(payload.fa_map);
            return -1;
        }
        if (prev_hash[0] != '\0') {
            if (strcmp(payload.prev, prev_hash) != 0) {
                fclose(fp);
                fprintf(stderr, "verify: prev mismatch at step %u\n", payload.step);
                free(payload.fa_map);
                return -1;
            }
        } else if (payload.prev[0] != '\0') {
            fclose(fp);
            fprintf(stderr, "verify: unexpected prev value at first block\n");
            free(payload.fa_map);
            return -1;
        }
        if (payload.step != expected_step) {
            fclose(fp);
            fprintf(stderr, "verify: step mismatch (expected %u got %u)\n", expected_step, payload.step);
            free(payload.fa_map);
            return -1;
        }
        if (payload.step > 0 && payload.parent != (int)(payload.step - 1)) {
            fclose(fp);
            fprintf(stderr, "verify: parent mismatch at step %u\n", payload.step);
            free(payload.fa_map);
            return -1;
        }
        if (cfg.hmac_key[0] != '\0' && payload.hmac_alg[0] != '\0') {
            char computed_hmac[65];
            kolibri_hmac_payload(&payload, cfg.hmac_key, computed_hmac);
            if (strcmp(computed_hmac, hmac_hex) != 0) {
                fclose(fp);
                fprintf(stderr, "verify: hmac mismatch at step %u\n", payload.step);
                free(payload.fa_map);
                return -1;
            }
        }
        strncpy(prev_hash, hash_hex, sizeof(prev_hash) - 1);
        ++expected_step;
        ++blocks;
        free(payload.fa_map);
    }
    fclose(fp);
    if (verbose) {
        printf("OK: chain verified (%zu blocks)\n", blocks);
    } else {
        printf("OK: chain verified (%zu blocks)\n", blocks);
    }
    return 0;
}
