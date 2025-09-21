#include "core.h"

#include "fmt_v5.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int kolibri_verify_file(const char *path, const kolibri_config_t *cfg, int verbose) {
    fmt_init_locale();
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "verify: unable to open %s\n", path);
        return -1;
    }
    kolibri_config_t empty_cfg;
    if (!cfg) {
        memset(&empty_cfg, 0, sizeof(empty_cfg));
        cfg = &empty_cfg;
    }
    char line[8192];
    char prev_hash[65] = "";
    unsigned expected_step = 0;
    size_t blocks = 0;
    int rc = -1;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        kolibri_payload_t payload;
        char hash_hex[65];
        char hmac_hex[65];
        if (kolibri_parse_block_line(line, len, &payload, hash_hex, hmac_hex) != 0) {
            fprintf(stderr, "verify: invalid block format\n");
            goto done;
        }
        char computed_hash[65];
        kolibri_hash_payload(&payload, computed_hash);
        if (strcmp(computed_hash, hash_hex) != 0) {
            fprintf(stderr, "verify: hash mismatch at step %u\n", payload.step);
            free(payload.fa_map);
            goto done;
        }
        if (prev_hash[0] != '\0') {
            if (strcmp(payload.prev, prev_hash) != 0) {
                fprintf(stderr, "verify: prev mismatch at step %u\n", payload.step);
                free(payload.fa_map);
                goto done;
            }
        } else if (payload.prev[0] != '\0') {
            fprintf(stderr, "verify: unexpected prev value at first block\n");
            free(payload.fa_map);
            goto done;
        }
        if (payload.step != expected_step) {
            fprintf(stderr, "verify: step mismatch (expected %u got %u)\n", expected_step, payload.step);
            free(payload.fa_map);
            goto done;
        }
        if (payload.step > 0 && payload.parent != (int)(payload.step - 1)) {
            fprintf(stderr, "verify: parent mismatch at step %u\n", payload.step);
            free(payload.fa_map);
            goto done;
        }
        if (cfg->salt[0] != '\0') {
            if (payload.salt[0] == '\0' || strcmp(payload.salt, cfg->salt) != 0) {
                fprintf(stderr, "verify: salt mismatch at step %u\n", payload.step);
                free(payload.fa_map);
                goto done;
            }
        }
        if (payload.hmac_alg[0] != '\0') {
            if (cfg->hmac_key[0] == '\0') {
                fprintf(stderr, "verify: hmac key required but not provided (step %u)\n", payload.step);
                free(payload.fa_map);
                goto done;
            }
            char computed_hmac[65];
            kolibri_hmac_payload(&payload, cfg->hmac_key, computed_hmac);
            if (strcmp(computed_hmac, hmac_hex) != 0) {
                fprintf(stderr, "verify: hmac mismatch at step %u\n", payload.step);
                free(payload.fa_map);
                goto done;
            }
        } else if (cfg->hmac_key[0] != '\0') {
            fprintf(stderr, "verify: expected hmac missing at step %u\n", payload.step);
            free(payload.fa_map);
            goto done;
        }
        strncpy(prev_hash, hash_hex, sizeof(prev_hash) - 1);
        ++expected_step;
        ++blocks;
        free(payload.fa_map);
    }
    if (ferror(fp)) {
        fprintf(stderr, "verify: read error on %s\n", path);
        goto done;
    }
    rc = 0;
    if (verbose) {
        printf("OK: chain verified (%zu blocks)\n", blocks);
    } else {
        printf("OK: chain verified (%zu blocks)\n", blocks);
    }

done:
    fclose(fp);
    return rc;
}
