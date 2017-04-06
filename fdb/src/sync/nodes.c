#include "nodes.h"

static char const *TBL_NODES = "nodes";

bool fdb_node_add(fdb_t *pdb, fuuid_t const *uuid, fnode_info_t const *info)
{
    if (!pdb || !uuid || !info)
        return false;

    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pdb, &transaction))
    {
        fdb_map_t map = {0};
        if (fdb_map_open(&transaction, TBL_NODES, FDB_MAP_CREATE, &map))
        {
            fdb_data_t const key = { sizeof *uuid, (void*)uuid };
            fdb_data_t const value = { sizeof *info, (void*)info };
            ret = fdb_map_put(&map, &transaction, &key, &value);
            fdb_transaction_commit(&transaction);
            fdb_map_close(&map);
        }
    }

    return ret;
}
