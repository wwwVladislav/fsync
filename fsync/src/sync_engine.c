#include "sync_engine.h"
#include "rstream.h"
#include <futils/log.h>
#include <futils/utils.h>
#include <futils/vector.h>
#include <futils/mutex.h>
#include <fcommon/limits.h>
#include <fcommon/messages.h>
#include <stdlib.h>
#include <string.h>

/*
         synchronization
    src ------ DATA ------> dst
        -- sync request -->
        <-- ostream req ---
        --- ostream ------>
        <-- [signature] ---
        --- ostream req -->
        <-- ostream -------
        ---- [delta] -----> apply
*/
typedef struct
{
    uint32_t          id;
    fsync_listener_t *listener;
} fsync_listener_info_t;

typedef struct
{
    uint32_t          listener_id;
    fistream_t       *pistream;
} fsync_src_t;

struct sync_engine
{
    volatile uint32_t       ref_counter;
    fuuid_t                 uuid;
    fmsgbus_t              *msgbus;
    frstream_factory_t     *stream_factory;
    fvector_t              *listeners;          // vector of fsync_listener_info_t

    pthread_mutex_t         sources_mutex;
    fvector_t              *sources;            // vector of fsync_src_t
};

static void fsync_engine_istream_listener(fsync_engine_t *pengine, fistream_t *pstream, uint32_t cookie)
{
}

static void fsync_engine_ostream_listener(fsync_engine_t *pengine, fostream_t *pstream, uint32_t cookie)
{
}

static void fsync_request_handler(fsync_engine_t *pengine, FMSG_TYPE(sync_request) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pengine->uuid, sizeof pengine->uuid) != 0)
        return;
    // TODO
}

static void fsync_engine_msgbus_retain(fsync_engine_t *pengine, fmsgbus_t *pmsgbus)
{
    pengine->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(pengine->msgbus, FSYNC_REQUEST,   (fmsg_handler_t)fsync_request_handler,  pengine);
}

static void fsync_engine_msgbus_release(fsync_engine_t *pengine)
{
    fmsgbus_unsubscribe(pengine->msgbus, FSYNC_REQUEST, (fmsg_handler_t)fsync_request_handler);
    fmsgbus_release(pengine->msgbus);
}

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
    pengine->uuid = *uuid;
    fsync_engine_msgbus_retain(pengine, pmsgbus);

    pengine->listeners = fvector(sizeof(fsync_listener_info_t), 0, 0);
    if (!pengine->listeners)
    {
        FS_ERR("Listeners vector wasn't created");
        fsync_engine_release(pengine);
        return 0;
    }

    pengine->sources_mutex = PTHREAD_MUTEX_INITIALIZER;
    pengine->sources = fvector(sizeof(fsync_src_t), 0, 0);
    if (!pengine->sources)
    {
        FS_ERR("Sources vector wasn't created");
        fsync_engine_release(pengine);
        return 0;
    }

    pengine->stream_factory = frstream_factory(pmsgbus, uuid);
    if (!pengine->stream_factory)
    {
        FS_ERR("Stream factory wasn't created");
        fsync_engine_release(pengine);
        return 0;
    }

    if (frstream_factory_istream_subscribe(pengine->stream_factory,
                                           (fristream_listener_t)fsync_engine_istream_listener,
                                           pengine) != FSUCCESS)
    {
        FS_ERR("Unable to subscribe istreams");
        fsync_engine_release(pengine);
        return 0;
    }

    if (frstream_factory_ostream_subscribe(pengine->stream_factory,
                                           (frostream_listener_t)fsync_engine_ostream_listener,
                                           pengine) != FSUCCESS)
    {
        FS_ERR("Unable to subscribe ostreams");
        fsync_engine_release(pengine);
        return 0;
    }

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
            fsync_engine_msgbus_release(pengine);
            if (pengine->stream_factory)
            {
                frstream_factory_istream_unsubscribe(pengine->stream_factory, (fristream_listener_t)fsync_engine_istream_listener);
                frstream_factory_ostream_unsubscribe(pengine->stream_factory, (frostream_listener_t)fsync_engine_ostream_listener);
                frstream_factory_release(pengine->stream_factory);
            }
            fvector_release(pengine->listeners);
            fvector_release(pengine->sources);
            free(pengine);
        }
    }
    else
        FS_ERR("Invalid sync engine");
}

static int listeners_comparer(const fsync_listener_info_t *lhs, const fsync_listener_info_t *rhs)
{
    return lhs->id < rhs->id;
}

ferr_t fsync_engine_register_listener(fsync_engine_t *pengine, fsync_listener_t *listener)
{
    if (!pengine || !listener)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    fsync_listener_info_t listener_info =
    {
        listener->id(listener),
        listener->retain(listener)
    };

    if (!fvector_push_back(&pengine->listeners, &listener_info))
    {
        listener_info.listener->release(listener_info.listener);
        FS_ERR("No memory for new listener");
        return FERR_NO_MEM;
    }

    fvector_qsort(pengine->listeners, (fvector_comparer_t)listeners_comparer);

    return FSUCCESS;
}

// src -- sync request --> dst
ferr_t fsync_engine_sync(fsync_engine_t *pengine, fuuid_t const *dst, uint32_t listener_id, fsync_metainf_t metainf, fistream_t *pstream)
{
    if (!pengine || !dst || !pstream)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    if (metainf.size > FMAX_METAINF_SIZE)
    {
        FS_ERR("Meta information size is too long");
        return FERR_INVALID_ARG;
    }

    fsync_src_t sync_src =
    {
        listener_id,
        pstream->retain(pstream)
    };

    ferr_t ret = FSUCCESS;

    fpush_lock(pengine->sources_mutex);
    if (!fvector_push_back(&pengine->sources, &sync_src))
    {
        FS_ERR("No memory for new synchronization stream");
        sync_src.pistream->release(sync_src.pistream);
        ret = FERR_NO_MEM;
    }
    fpop_lock();

    if (ret != FSUCCESS)
        return ret;

    FMSG(sync_request, req, pengine->uuid, *dst,
        listener_id,
        metainf.size
    );
    memcpy(req.metainf, metainf.data, metainf.size);

    ret = fmsgbus_publish(pengine->msgbus, FSYNC_REQUEST, (fmsg_t const *)&req);
    if (ret != FSUCCESS)
    {
        sync_src.pistream->release(sync_src.pistream);
        fpush_lock(pengine->sources_mutex);
        fvector_pop_back(&pengine->sources);
        fpop_lock();
    }

    return ret;
}
