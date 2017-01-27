#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <futils/log.h>
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <filink/interface.h>
#include <fsync/sync.h>

static char const *FDB_DATA_SOURCE = "data";

enum
{
    FDB_MAX_READERS = 1,
    FDB_MAP_SIZE    = 16 * 1024 * 1024,
    FDB_MAX_DBS     = 4
};

struct fcore
{
    fuuid_t    uuid;
    fmsgbus_t *msgbus;
    filink_t  *ilink;
    fsync_t   *sync;
    fdb_t     *db;
};

fcore_t *fcore_start(char const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return 0;
    }

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

    if (!fuuid_gen(&pcore->uuid))
    {
        FS_ERR("UUID generation failed for node");
        fcore_stop(pcore);
        return 0;
    }

    if (fmsgbus_create(&pcore->msgbus) != FSUCCESS)
    {
        FS_ERR("Messages bus not initialized");
        fcore_stop(pcore);
        return 0;
    }

    pcore->ilink = filink_bind(pcore->msgbus, addr, &pcore->uuid);
    if (!pcore->ilink)
    {
        fcore_stop(pcore);
        return 0;
    }

    FS_INFO("Started node UUID: %llx%llx", pcore->uuid.data.u64[0], pcore->uuid.data.u64[1]);
    return pcore;
}

void fcore_stop(fcore_t *pcore)
{
    if (pcore)
    {
        filink_unbind(pcore->ilink);
        fsync_free(pcore->sync);
        fmsgbus_release(pcore->msgbus);
        fdb_release(pcore->db);
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

    pcore->sync = fsync_create(pcore->msgbus, pcore->db, dir, &pcore->uuid);

    return true;
}
