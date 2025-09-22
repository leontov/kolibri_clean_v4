#ifndef KOLIBRI_SYNC_H
#define KOLIBRI_SYNC_H
#include "config.h"
#include <stdbool.h>

bool kolibri_sync_service_start(const kolibri_config_t* cfg, const char* chain_path);
void kolibri_sync_service_stop(void);
bool kolibri_sync_tick(const kolibri_config_t* cfg, const char* chain_path);

#endif
