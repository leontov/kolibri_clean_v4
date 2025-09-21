#include "core.h"
#include "chainio.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv){
    const char* cfg_path = (argc>1)? argv[1] : "configs/kolibri.json";
    kolibri_config_t cfg; kolibri_load_config(&cfg, cfg_path);

    char prev[65]={0}, hash[65]={0};
    for(int step=1; step<=cfg.steps; ++step){
        ReasonBlock b={0};
        if(!kolibri_step(&cfg, step, prev[0]?prev:NULL, &b, hash)){
            fprintf(stderr,"[ERROR] step %d failed\n", step);
            return 1;
        }
        strncpy(prev, hash, 65);
    }
    printf("%s\n", prev);
    return 0;
}
