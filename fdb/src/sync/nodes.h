#ifndef FSYNC_NODES_H_FDB
#define FSYNC_NODES_H_FDB
#include <fcommon/limits.h>
#include <futils/uuid.h>
#include <stdbool.h>
#include "../db.h"

typedef struct
{
    char address[FMAX_ADDR];
} fdb_node_info_t;

typedef struct fdb_nodes_iterator fdb_nodes_iterator_t;

bool fdb_node_add(fdb_t *pdb, fuuid_t const *uuid, fdb_node_info_t const *info);
bool fdb_nodes_iterator(fdb_t *pdb, fdb_nodes_iterator_t **it);
void fdb_nodes_iterator_delete(fdb_nodes_iterator_t *it);
bool fdb_nodes_next(fdb_nodes_iterator_t *it, fuuid_t *uuid, fdb_node_info_t *info);

#endif
