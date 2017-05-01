#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <futils/log.h>
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <filink/interface.h>
#include <fsync/sync.h>
#include <fdb/sync/config.h>
#include <fdb/sync/nodes.h>

static char const *FDB_DATA_SOURCE = "data";

enum
{
    FDB_MAX_READERS = 1,
    FDB_MAP_SIZE    = 64 * 1024 * 1024,
    FDB_MAX_DBS     = 8     // config, nodes
};

struct fcore
{
    fmsgbus_t *msgbus;
    filink_t  *ilink;
    fsync_t   *sync;
    fdb_t     *db;
    fconfig_t  config;
};

fcore_t *fcore_start(char const *addr)
{
    fcore_t *pcore = malloc(sizeof(fcore_t));
    if (!pcore)
    {
        FS_ERR("Unable to allocate memory for core");
        return 0;
    }
    memset(pcore, 0, sizeof *pcore);

    pcore->db = fdb_open(FDB_DATA_SOURCE, FDB_MAX_DBS, FDB_MAX_READERS, FDB_MAP_SIZE);
    if (!pcore->db)
    {
        FS_ERR("Unable to open the DB");
        fcore_stop(pcore);
        return 0;
    }

    if (!fdb_load_config(pcore->db, &pcore->config))
    {
        if (!addr)
        {
            FS_ERR("Invalid address");
            return 0;
        }

        if (!fuuid_gen(&pcore->config.uuid))
        {
            FS_ERR("UUID generation failed for node");
            fcore_stop(pcore);
            return 0;
        }
    }

    if (addr)
        strncpy(pcore->config.address, addr, sizeof pcore->config.address);

    if (!fdb_save_config(pcore->db, &pcore->config))
        FS_ERR("Current configuration doesn't saved");

    if (fmsgbus_create(&pcore->msgbus) != FSUCCESS)
    {
        FS_ERR("Messages bus not initialized");
        fcore_stop(pcore);
        return 0;
    }

    pcore->ilink = filink_bind(pcore->msgbus, pcore->db, pcore->config.address, &pcore->config.uuid);
    if (!pcore->ilink)
    {
        fcore_stop(pcore);
        return 0;
    }

    char buf[2 * sizeof(fuuid_t) + 1] = { 0 };
    FS_INFO("Started node UUID: %s", fuuid2str(&pcore->config.uuid, buf, sizeof buf));
    FS_INFO("Listening: %s", pcore->config.address);
    return pcore;
}

void fcore_stop(fcore_t *pcore)
{
    if (pcore)
    {
        filink_unbind(pcore->ilink);
        filink_release(pcore->ilink);
        fsync_free(pcore->sync);
        fmsgbus_release(pcore->msgbus);
        fdb_release(pcore->db);
        memset(pcore, 0, sizeof *pcore);
        free(pcore);
    }
}

bool fcore_connect(fcore_t *pcore, char const *addr)
{
    if (!pcore)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    if (!addr)
    {
        FS_ERR("Invalid address");
        return false;
    }

    return filink_connect(pcore->ilink, addr);
}

bool fcore_sync(fcore_t *pcore, char const *dir)
{
    if (!pcore || !dir)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    if (pcore->sync)
        fsync_free(pcore->sync);

    pcore->sync = fsync_create(pcore->msgbus, pcore->db, dir, &pcore->config.uuid);

    return true;
}

struct fcore_nodes_iterator
{
    filink_t             *ilink;
    fdb_transaction_t     transaction;
    fdb_nodes_iterator_t *nodes_iterator;
};

fcore_nodes_iterator_t *fcore_nodes_iterator(fcore_t *pcore)
{
    if (!pcore)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    fcore_nodes_iterator_t *it = malloc(sizeof(fcore_nodes_iterator_t));
    if (!it)
        return false;
    memset(it, 0, sizeof(fcore_nodes_iterator_t));
    it->ilink = filink_retain(pcore->ilink);

    if (!fdb_transaction_start(pcore->db, &it->transaction))
    {
        fcore_nodes_iterator_free(it);
        return 0;
    }

    fdb_nodes_t *nodes = fdb_nodes(&it->transaction);
    if (!nodes)
    {
        fcore_nodes_iterator_free(it);
        return 0;
    }

    it->nodes_iterator = fdb_nodes_iterator(nodes, &it->transaction);
    fdb_nodes_release(nodes);

    if (!it->nodes_iterator)
    {
        fcore_nodes_iterator_free(it);
        return 0;
    }

    return it;
}

void fcore_nodes_iterator_free(fcore_nodes_iterator_t *it)
{
    if (it)
    {
        filink_release(it->ilink);
        fdb_transaction_abort(&it->transaction);
        fdb_nodes_iterator_free(it->nodes_iterator);
        free(it);
    }
}

bool fcore_nodes_first(fcore_nodes_iterator_t *it, fcore_node_info_t *info)
{
    if (!it || !info)
        return false;

    fdb_node_info_t node_info = { 0 };
    bool ret = fdb_nodes_first(it->nodes_iterator, &info->uuid, &node_info);
    if (ret)
    {
        strncpy(info->address, node_info.address, sizeof info->address);
        info->connected = filink_is_connected(it->ilink, &info->uuid);
        return true;
    }

    return false;
}

bool fcore_nodes_next(fcore_nodes_iterator_t *it, fcore_node_info_t *info)
{
    if (!it || !info)
        return false;

    fdb_node_info_t node_info = { 0 };
    bool ret = fdb_nodes_next(it->nodes_iterator, &info->uuid, &node_info);
    if (ret)
    {
        strncpy(info->address, node_info.address, sizeof info->address);
        info->connected = filink_is_connected(it->ilink, &info->uuid);
        return true;
    }

    return false;
}
