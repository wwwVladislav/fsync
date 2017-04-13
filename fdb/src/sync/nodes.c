#include "nodes.h"
#include <binn.h>
#include <string.h>
#include <stdlib.h>

static char const *TBL_NODES = "nodes";

static binn * fdb_node_marshal(fdb_node_info_t const *info)
{
    binn *obj = binn_object();
    binn_object_set_str(obj, "address", (char*)info->address);
    return obj;
}

static bool fdb_node_unmarshal(fdb_node_info_t *info, void const *data)
{
    if (!info || !data)
        return false;
    binn *obj = binn_open((void *)data);
    if (!obj)
        return false;
    strncpy(info->address, binn_object_str(obj, "address"), sizeof info->address);
    binn_free(obj);
    return true;
}

bool fdb_node_add(fdb_t *pdb, fuuid_t const *uuid, fdb_node_info_t const *info)
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
            binn *obj = fdb_node_marshal(info);

            fdb_data_t const key = { sizeof *uuid, (void*)uuid };
            fdb_data_t const value = { binn_size(obj), binn_ptr(obj) };
            ret = fdb_map_put(&map, &transaction, &key, &value);

            fdb_transaction_commit(&transaction);
            fdb_map_close(&map);
            binn_free(obj);
        }
    }

    return ret;
}

struct fdb_nodes_iterator
{
    fdb_transaction_t transaction;
    fdb_map_t map;
    fdb_cursor_t cursor;
};

bool fdb_nodes_iterator(fdb_t *pdb, fdb_nodes_iterator_t **pit)
{
    if (!pdb || !pit)
        return false;
    fdb_nodes_iterator_t *it = *pit = malloc(sizeof(fdb_nodes_iterator_t));
    if (!it)
        return false;
    memset(it, 0, sizeof(fdb_nodes_iterator_t));
    return fdb_transaction_start(pdb, &it->transaction)
           && fdb_map_open(&it->transaction, TBL_NODES, FDB_MAP_CREATE, &it->map)
           && fdb_cursor_open(&it->map, &it->transaction, &it->cursor);
}

void fdb_nodes_iterator_delete(fdb_nodes_iterator_t *it)
{
    if (it)
    {
        fdb_cursor_close(&it->cursor);
        fdb_transaction_abort(&it->transaction);
        fdb_map_close(&it->map);
        free(it);
    }
}

bool fdb_nodes_next(fdb_nodes_iterator_t *it, fuuid_t *uuid, fdb_node_info_t *info)
{
    if (!it || !uuid || !info)
        return false;
    fdb_data_t key = { 0 };
    fdb_data_t value = { 0 };
    bool ret = fdb_cursor_get(&it->cursor, &key, &value, FDB_NEXT);
    if (!ret)
        return false;
    memcpy(uuid, key.data, key.size);
    return fdb_node_unmarshal(info, value.data);
}
