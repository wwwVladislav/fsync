#include "sync_engine.h"
#include <futils/log.h>
#include <stdlib.h>
#include <string.h>

struct sync_engine
{
    volatile uint32_t   ref_counter;
    fmsgbus_t          *pmsgbus;
    fuuid_t             uuid;
};

fsync_engine_t *fsync_engine(fmsgbus_t *pmsgbus, fuuid_t const *uuid)
{
    if (!pmsgbus || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fsync_engine_t *pengine = malloc(sizeof(fsync_engine_t));
    if (!pengine)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(pengine, 0, sizeof *pengine);

    pengine->ref_counter = 1;
    pengine->pmsgbus = fmsgbus_retain(pmsgbus);
    pengine->uuid = *uuid;

    return pengine;
}

fsync_engine_t *fsync_engine_retain(fsync_engine_t *pengine)
{
    if (pengine)
        pengine->ref_counter++;
    else
        FS_ERR("Invalid sync engine");
    return pengine;
}

void fsync_engine_release(fsync_engine_t *pengine)
{
    if (pengine)
    {
        if (!pengine->ref_counter)
            FS_ERR("Invalid sync engine");
        else if (!--pengine->ref_counter)
        {
            fmsgbus_release(pengine->pmsgbus);
            free(pengine);
        }
    }
    else
        FS_ERR("Invalid sync engine");
}
