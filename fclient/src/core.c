#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <futils/log.h>
#include <futils/uuid.h>
#include <filink/interface.h>

struct fcore
{
    filink_t *ilink;
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

    fuuid_t uuid;
    if (!fuuid_gen(&uuid))
    {
        FS_ERR("UUID generation failed for node");
        fcore_stop(pcore);
        return 0;
    }

    pcore->ilink = filink_bind(addr, &uuid);
    if (!pcore->ilink)
    {
        fcore_stop(pcore);
        return 0;
    }

    FS_INFO("Started node UUID: %llx%llx", uuid.data.u64[0], uuid.data.u64[1]);
    return pcore;
}

void fcore_stop(fcore_t *pcore)
{
    if (pcore)
    {
        filink_unbind(pcore->ilink);
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
    return true;
}
