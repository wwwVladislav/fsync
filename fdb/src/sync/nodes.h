#ifndef FSYNC_NODES_H_FDB
#define FSYNC_NODES_H_FDB
#include <fcommon/limits.h>
#include <futils/uuid.h>
#include <stdbool.h>
#include "../db.h"

typedef struct
{
    char address[FMAX_ADDR];
} fnode_info_t;

bool fdb_node_add(fdb_t *pdb, fuuid_t const *uuid, fnode_info_t const *info);

#endif
