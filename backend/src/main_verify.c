#include "chainio.h"
#include "config.h"
#include "reason.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char* prog){
    fprintf(stderr,
            "Usage: %s [--config PATH] [CHAIN_PATH]\n"
            "  --config PATH   Path to runtime configuration (default: configs/kolibri.json)\n"
            "  CHAIN_PATH      Path to chain log (default: logs/chain.jsonl)\n",
            prog);
}

int main(int argc, char** argv){
    const char* chain_path = "logs/chain.jsonl";
    const char* config_path = "configs/kolibri.json";
    bool chain_arg_consumed = false;

    for(int i = 1; i < argc; ++i){
        if(strcmp(argv[i], "--config") == 0){
            if(i + 1 >= argc){
                fprintf(stderr, "--config requires a path argument\n");
                print_usage(argv[0]);
                return 1;
            }
            config_path = argv[++i];
        } else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0){
            print_usage(argv[0]);
            return 0;
        } else if(!chain_arg_consumed){
            chain_path = argv[i];
            chain_arg_consumed = true;
        } else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    kolibri_config_t cfg;
    kolibri_load_config(&cfg, config_path);
    if(chain_verify(chain_path, stdout, &cfg)) return 0;
    return 1;
}
