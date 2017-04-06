#ifndef FSYNC_CONFIG_H_FDB
#define FSYNC_CONFIG_H_FDB
#include <fcommon/limits.h>
#include <futils/uuid.h>
#include "../db.h"

typedef struct
{
    fuuid_t uuid;
    char address[FMAX_ADDR];
} fconfig_t;

bool fdb_load_config(fdb_t *pdb, fconfig_t *config);
bool fdb_save_config(fdb_t *pdb, fconfig_t const *config);

#endif
