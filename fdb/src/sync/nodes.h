#ifndef FSYNC_NODES_H_FDB
#define FSYNC_NODES_H_FDB
#include <fcommon/limits.h>
#include <stdbool.h>
#include "../db.h"

typedef struct
{
    char address[FMAX_ADDR];
} fnode_info_t;

// bool fdb_sync_file_add(fuuid_t const *uuid, ffile_info_t *info);
// fdb_t*

#endif
