#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool kolibri_load_config(kolibri_config_t* c, const char* json_path){
    c->steps = 30;
    c->depth_max = 2;
    c->depth_decay = 0.7;
    c->quorum = 0.6;
    c->temperature = 0.15;
    c->eff_threshold = 0.8;
    c->max_complexity = 32.0;
    c->seed = 987654321ULL;

    if(!json_path) return true;
    FILE* f = fopen(json_path,"rb");
    if(!f) return true;
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    char* buf=(char*)malloc(n+1); if(!buf){ fclose(f); return true; }
    size_t readn=fread(buf,1,(size_t)n,f);
    fclose(f);
    if(readn!=(size_t)n){ free(buf); return true; }
    buf[n]=0;
    char* p=buf;
    #define GETI(k,dst) do{ char* q=strstr(p,k); if(q){ q=strchr(q,':'); if(q){ dst=atoi(q+1);} } }while(0)
    #define GETD(k,dst) do{ char* q=strstr(p,k); if(q){ q=strchr(q,':'); if(q){ dst=atof(q+1);} } }while(0)
    #define GETU(k,dst) do{ char* q=strstr(p,k); if(q){ q=strchr(q,':'); if(q){ dst=(unsigned long long)atoll(q+1);} } }while(0)
    GETI("\"steps\"", c->steps);
    GETI("\"depth_max\"", c->depth_max);
    GETD("\"depth_decay\"", c->depth_decay);
    GETD("\"quorum\"", c->quorum);
    GETD("\"temperature\"", c->temperature);
    GETD("\"eff_threshold\"", c->eff_threshold);
    GETD("\"max_complexity\"", c->max_complexity);
    GETU("\"seed\"", c->seed);
    free(buf);
    return true;
}
