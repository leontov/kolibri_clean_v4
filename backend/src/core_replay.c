#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int kolibri_replay(const kolibri_config_t *cfg) {
    if (!cfg) {
        fprintf(stderr, "replay: missing config\n");
        return -1;
    }
    FILE *fp = fopen(KOLIBRI_DEFAULT_LOG, "r");
    if (!fp) {
        fprintf(stderr, "replay: unable to open %s\n", KOLIBRI_DEFAULT_LOG);
        return -1;
    }
    char line[8192];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        kolibri_payload_t payload;
        if (kolibri_parse_block_line(line, len, &payload, NULL, NULL) == 0) {
            printf("replay step %u eff=%.6f compl=%.6f fa=%s\n", payload.step, payload.eff, payload.compl, payload.fa);
            free(payload.fa_map);
        }
    }
    fclose(fp);
    return 0;
}
