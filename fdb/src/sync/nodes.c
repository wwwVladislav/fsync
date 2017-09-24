#include "nodes.h"
#include <futils/log.h>
#include <binn.h>
#include <string.h>
#include <stdlib.h>

static char const *TBL_NODES = "sys/nodes";

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

struct fdb_nodes
{
    volatile uint32_t   ref_counter;
    fdb_map_t           nodes_map;
};

fdb_nodes_t *fdb_nodes(fdb_transaction_t *transaction)
{
    if (!transaction)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdb_nodes_t *nodes = malloc(sizeof(fdb_nodes_t));
    if (!nodes)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(nodes, 0, sizeof *nodes);

    nodes->ref_counter = 1;

    if (!fdb_map_open(transaction, TBL_NODES, FDB_MAP_CREATE, &nodes->nodes_map))
    {
        fdb_nodes_release(nodes);
        return 0;
    }

    return nodes;
}

fdb_nodes_t *fdb_nodes_retain(fdb_nodes_t *nodes)
{
    if (nodes)
        nodes->ref_counter++;
    else
        FS_ERR("Invalid nodes map");
    return nodes;
}

void fdb_nodes_release(fdb_nodes_t *nodes)
{
    if (nodes)
    {
        if (!nodes->ref_counter)
            FS_ERR("Invalid nodes map");
        else if (!--nodes->ref_counter)
        {
            fdb_map_close(&nodes->nodes_map);
            free(nodes);
        }
    }
    else
        FS_ERR("Invalid nodes map");
}

bool fdb_node_add(fdb_nodes_t *nodes, fdb_transaction_t *transaction, fuuid_t const *uuid, fdb_node_info_t const *info)
{
    if (!transaction || !transaction || !uuid || !info)
        return false;

    binn *obj = fdb_node_marshal(info);

    fdb_data_t const key = { sizeof *uuid, (void*)uuid };
    fdb_data_t const value = { binn_size(obj), binn_ptr(obj) };
    bool ret = fdb_map_put(&nodes->nodes_map, transaction, &key, &value);
    binn_free(obj);
    return ret;
}

struct fdb_nodes_iterator
{
    fdb_nodes_t *nodes;
    fdb_cursor_t cursor;
};

fdb_nodes_iterator_t *fdb_nodes_iterator(fdb_nodes_t *nodes, fdb_transaction_t *transaction)
{
    if (!nodes || !transaction)
        return false;
    fdb_nodes_iterator_t *it = malloc(sizeof(fdb_nodes_iterator_t));
    if (!it)
        return false;
    memset(it, 0, sizeof(fdb_nodes_iterator_t));
    it->nodes = fdb_nodes_retain(nodes);
    if (!fdb_cursor_open(&nodes->nodes_map, transaction, &it->cursor))
    {
        fdb_nodes_iterator_free(it);
        it = 0;
    }
    return it;
}

void fdb_nodes_iterator_free(fdb_nodes_iterator_t *it)
{
    if (it)
    {
        fdb_nodes_release(it->nodes);
        fdb_cursor_close(&it->cursor);
        free(it);
    }
}

bool fdb_nodes_first(fdb_nodes_iterator_t *it, fuuid_t *uuid, fdb_node_info_t *info)
{
    if (!it || !uuid || !info)
        return false;
    fdb_data_t key = { 0 };
    fdb_data_t value = { 0 };
    bool ret = fdb_cursor_get(&it->cursor, &key, &value, FDB_FIRST);
    if (!ret)
        return false;
    memcpy(uuid, key.data, key.size);
    return fdb_node_unmarshal(info, value.data);
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
