#include "core.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int core_replay_chain(const char *path, char *hash_out, size_t hash_out_size, char *err, size_t err_size) {
    if (hash_out == NULL || hash_out_size < KOLIBRI_HASH_TEXT) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "buffer too small");
        }
        return -1;
    }
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "cannot open %s", path);
        }
        return -1;
    }
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    hash_out[0] = '\0';
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        const char *needle = "\"hash\":\"";
        char *pos = strstr(line, needle);
        if (pos == NULL) {
            continue;
        }
        pos += strlen(needle);
        if ((size_t)(pos - line) + KOLIBRI_HASH_HEX > (size_t)linelen) {
            continue;
        }
        strncpy(hash_out, pos, KOLIBRI_HASH_HEX);
        hash_out[KOLIBRI_HASH_HEX] = '\0';
    }
    free(line);
    fclose(fp);
    if (hash_out[0] == '\0') {
        if (err && err_size > 0) {
            snprintf(err, err_size, "no blocks");
        }
        return -1;
    }
    return 0;
}
