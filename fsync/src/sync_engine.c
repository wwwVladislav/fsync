#include "sync_engine.h"
#include <futils/log.h>
#include <futils/utils.h>
#include <fcommon/limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint32_t          key;
    fsync_listener_t *listener;
} fsync_listener_info_t;

struct sync_engine
{
    volatile uint32_t       ref_counter;
    fmsgbus_t              *pmsgbus;
    fuuid_t                 uuid;
    volatile uint32_t       listeners_num;
    fsync_listener_info_t   listeners[FSYNC_ENGINE_MAX_LISTENERS];
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

ferr_t fsync_engine_register_listener(fsync_engine_t *pengine, uint32_t key, fsync_listener_t *listener)
{
    if (!pengine || !listener)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    if (pengine->listeners_num >= FARRAY_SIZE(pengine->listeners))
    {
        FS_ERR("Maximum allowed number of listeners is reached");
        return FERR_OVERFLOW;
    }

    fsync_listener_info_t *listener_info = pengine->listeners + pengine->listeners_num + 1;
    listener_info->key = key;
    listener_info->listener = listener->retain(listener);
    pengine->listeners_num++;

    return FSUCCESS;
}
