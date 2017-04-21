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

typedef struct fdb_nodes fdb_nodes_t;
typedef struct fdb_nodes_iterator fdb_nodes_iterator_t;

fdb_nodes_t *fdb_nodes(fdb_transaction_t *transaction);
fdb_nodes_t *fdb_nodes_retain(fdb_nodes_t *nodes);
void fdb_nodes_release(fdb_nodes_t *nodes);

bool                  fdb_node_add(fdb_nodes_t *nodes, fdb_transaction_t *transaction, fuuid_t const *uuid, fdb_node_info_t const *info);
fdb_nodes_iterator_t *fdb_nodes_iterator(fdb_nodes_t *nodes, fdb_transaction_t *transaction);
void                  fdb_nodes_iterator_free(fdb_nodes_iterator_t *it);
bool                  fdb_nodes_first(fdb_nodes_iterator_t *it, fuuid_t *uuid, fdb_node_info_t *info);
bool                  fdb_nodes_next(fdb_nodes_iterator_t *it, fuuid_t *uuid, fdb_node_info_t *info);

#endif
