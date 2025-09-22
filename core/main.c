#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kolibri.h"

static void usage(const char *name) {
    fprintf(stderr,
            "Usage: %s [--ticks N] [--seed S] [--depth D] [--bootstrap N] [--no-bootstrap]\n",
            name);
}

int main(int argc, char **argv) {
    int ticks = 5;
    int depth = 3;
    unsigned int seed = 1337;
    int bootstrap_steps = 1000;
    int do_bootstrap = 1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            ticks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (unsigned int)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            depth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--bootstrap") == 0 && i + 1 < argc) {
            bootstrap_steps = atoi(argv[++i]);
            do_bootstrap = 1;
        } else if (strcmp(argv[i], "--no-bootstrap") == 0) {
            do_bootstrap = 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (kol_init((uint8_t)depth, seed) != 0) {
        fprintf(stderr, "kol_init failed\n");
        return 1;
    }
    if (do_bootstrap && bootstrap_steps > 0) {
        KolBootstrapReport report;
        if (kol_bootstrap(bootstrap_steps, &report) != 0) {
            fprintf(stderr, "bootstrap failed\n");
            kol_reset();
            return 1;
        }
        printf("bootstrap: executed=%u start=%u final_eff=%.4f final_compl=%.2f\n",
               report.executed, report.start_step, report.final_eff, report.final_compl);
        if (report.best_formula[0]) {
            printf("best: step=%u eff=%.4f compl=%.2f formula=%s\n", report.best_step,
                   report.best_eff, report.best_compl, report.best_formula);
        }
    }
    for (int i = 0; i < ticks; ++i) {
        if (kol_tick() != 0) {
            fprintf(stderr, "tick failed\n");
            kol_reset();
            return 1;
        }
        printf("tick %d: eff=%.4f compl=%.2f\n", i + 1, kol_eff(), kol_compl());
    }
    char buf[2048];
    int len = kol_tail_json(buf, sizeof(buf), 5);
    if (len > 0) {
        printf("tail=%.*s\n", len, buf);
    }
    kol_reset();
    return 0;
}
