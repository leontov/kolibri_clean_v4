#include "core.h"
#include "chainio.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void ensure_logs_dir(void){
#ifdef _WIN32
    _mkdir("logs");
#else
    struct stat st={0};
    if(stat("logs",&st)!=0) mkdir("logs", 0755);
#endif
}

int main(int argc, char** argv){
    const char* cfg_path = (argc>1)? argv[1] : "configs/kolibri.json";
    kolibri_config_t cfg; kolibri_load_config(&cfg, cfg_path);

    ensure_logs_dir();
    /* Config registry snapshot (Phase 2 roadmap item 95). */
    if(!kolibri_config_write_snapshot(&cfg, "logs/config_snapshot.json")){
        fprintf(stderr, "[WARN] unable to write config snapshot registry\n");
    }
    char prev[65]={0}, hash[65]={0};
    for(int step=1; step<=cfg.steps; ++step){
        ReasonBlock b={0};
        if(!kolibri_step(&cfg, step, prev[0]?prev:NULL, &b, hash)){
            fprintf(stderr,"[ERROR] step %d failed\n", step);
            return 1;
        }
        printf("[STEP %d] eff=%.4f compl=%.1f formula=%s hash=%s\n",
               step, b.eff, b.compl, b.formula, hash);
        strncpy(prev, hash, 65);
    }
    if(!chain_verify("logs/chain.jsonl", stdout)){
        fprintf(stderr, "self-check verification failed\n");
        return 1;
    }
    return 0;
}
