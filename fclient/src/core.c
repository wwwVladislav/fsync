#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <futils/log.h>
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <filink/interface.h>
#include <fsync/fsync.h>
#include <fsync/search_engine.h>
#include <fdb/sync/config.h>
#include <fdb/sync/nodes.h>
#include <fdb/sync/dirs.h>
#include <fdb/sync/files.h>

static char const *FDB_DATA_SOURCE = "data";

enum
{
    FDB_MAX_READERS = 1,
    FDB_MAP_SIZE    = 64 * 1024 * 1024,
    FDB_MAX_DBS     = 8     // config, nodes
};

struct fcore
{
    fmsgbus_t        *msgbus;
    fdb_t            *db;
    filink_t         *ilink;
    fsync_t          *sync;
    fsearch_engine_t *search_engine;
    fconfig_t         config;
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

    if (fmsgbus_create(&pcore->msgbus, FMSGBUS_THREADS_NUM) != FSUCCESS)
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

    pcore->search_engine = fsearch_engine(pcore->msgbus, pcore->db, &pcore->config.uuid);
    if (!pcore->search_engine)
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
        fsync_release(pcore->sync);
        fsearch_engine_release(pcore->search_engine);
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
        fsync_release(pcore->sync);

    pcore->sync = fsync_create(pcore->msgbus, pcore->db, dir, &pcore->config.uuid);

    return true;
}

bool fcore_index(fcore_t *pcore, char const *dir)
{
    if (!pcore || !dir)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    return fsearch_engine_add_dir(pcore->search_engine, dir);
}

bool fcore_find(fcore_t *pcore, char const *file, fuuid_t *uuid)
{
    if (!pcore || !file || !uuid)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pcore->db, &transaction))
    {
        fdb_files_t *files = fdb_files(&transaction, &pcore->config.uuid);
        if (files)
        {
            ret = fdb_files_find(files, &transaction, file);
            if (ret)
                *uuid = pcore->config.uuid;
            fdb_files_release(files);
        }
        fdb_transaction_abort(&transaction);
    }

    return ret;
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

struct fcore_dirs_iterator
{
    fdb_transaction_t     transaction;
    fdb_dirs_iterator_t  *dirs_iterator;
};

fcore_dirs_iterator_t *fcore_dirs_iterator(fcore_t *pcore)
{
    if (!pcore)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    fcore_dirs_iterator_t *it = malloc(sizeof(fcore_dirs_iterator_t));
    if (!it)
        return false;
    memset(it, 0, sizeof(fcore_dirs_iterator_t));

    if (!fdb_transaction_start(pcore->db, &it->transaction))
    {
        fcore_dirs_iterator_free(it);
        return 0;
    }

    fdb_dirs_t *dirs = fdb_dirs(&it->transaction);
    if (!dirs)
    {
        fcore_dirs_iterator_free(it);
        return 0;
    }

    it->dirs_iterator = fdb_dirs_iterator(dirs, &it->transaction);
    fdb_dirs_release(dirs);

    if (!it->dirs_iterator)
    {
        fcore_dirs_iterator_free(it);
        return 0;
    }

    return it;
}

void fcore_dirs_iterator_free(fcore_dirs_iterator_t *it)
{
    if (it)
    {
        fdb_transaction_abort(&it->transaction);
        fdb_dirs_iterator_free(it->dirs_iterator);
        free(it);
    }
}

bool fcore_dirs_first(fcore_dirs_iterator_t *it, fcore_dir_info_t *info)
{
    if (!it || !info)
        return false;

    fdir_info_t dir_info = { 0 };
    bool ret = fdb_dirs_iterator_first(it->dirs_iterator, &dir_info);
    if (ret)
    {
        strncpy(info->path, dir_info.path, sizeof info->path);
        return true;
    }

    return false;
}

bool fcore_dirs_next(fcore_dirs_iterator_t *it, fcore_dir_info_t *info)
{
    if (!it || !info)
        return false;

    fdir_info_t dir_info = { 0 };
    bool ret = fdb_dirs_iterator_next(it->dirs_iterator, &dir_info);
    if (ret)
    {
        strncpy(info->path, dir_info.path, sizeof info->path);
        return true;
    }

    return false;
}
