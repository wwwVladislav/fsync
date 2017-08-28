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
#include <time.h>

/*
 *        synchronization
 *  src ------ DATA ------> dst
 *      -- sync request -->
 *      <-- ostream req ---
        --- ostream ------>
        <-- [signature] ---
        --- ostream req -->
        <-- ostream -------
        ---- [delta] -----> apply
*/

typedef struct
{
    uint32_t                sync_id;
    fuuid_t                 dst;
    uint32_t                listener_id;
    time_t                  time;
    fistream_t             *pistream;
} fsync_outgoing_stream_t;

typedef struct
{
    uint32_t                sync_id;
    fuuid_t                 src;
    uint32_t                listener_id;
    time_t                  time;
    fostream_t             *signature_ostream;
} fsync_incoming_stream_t;

struct sync_engine
{
    volatile uint32_t       ref_counter;
    volatile uint32_t       sync_id;
    fuuid_t                 uuid;
    fmsgbus_t              *msgbus;
    frstream_factory_t     *stream_factory;

    pthread_mutex_t         listeners_mutex;
    fvector_t              *listeners;                  // vector of fsync_listener_t

    pthread_mutex_t         outgoing_streams_mutex;
    fvector_t              *outgoing_streams;           // vector of fsync_outgoing_stream_t

    pthread_mutex_t         incoming_streams_mutex;
    fvector_t              *incoming_streams;           // vector of fsync_incoming_stream_t
};

static int fsync_listeners_cmp(fsync_listener_t const *lhs, fsync_listener_t const *rhs)
{
    return (int)lhs->id - rhs->id;
}

static int fsync_outgoing_stream_cmp(fsync_outgoing_stream_t const *lhs, fsync_outgoing_stream_t const *rhs)
{
    return (int)lhs->sync_id - rhs->sync_id;
}

static int fsync_incoming_stream_cmp(fsync_incoming_stream_t const *lhs, fsync_incoming_stream_t const *rhs)
{
    int a = (int)lhs->sync_id - rhs->sync_id;
    if (a) return a;
    return fuuid_cmp(&lhs->src, &rhs->src);
}

static bool fsync_add_outgoing_stream(fsync_engine_t *pengine, fsync_outgoing_stream_t const *outgoing_stream)
{
    outgoing_stream->pistream->retain(outgoing_stream->pistream);

    bool ret = true;
    fpush_lock(pengine->outgoing_streams_mutex);
    if (fvector_push_back(&pengine->outgoing_streams, outgoing_stream))
        fvector_qsort(pengine->outgoing_streams, (fvector_comparer_t)fsync_outgoing_stream_cmp);
    else
    {
        outgoing_stream->pistream->release(outgoing_stream->pistream);
        FS_ERR("No memory for new synchronization stream");
        ret = false;
    }
    fpop_lock();
    return ret;
}

static void fsync_remove_outgoing_stream(fsync_engine_t *pengine, uint32_t sync_id)
{
    fsync_outgoing_stream_t const src =
    {
        sync_id
    };

    fpush_lock(pengine->outgoing_streams_mutex);
    fsync_outgoing_stream_t const *psrc = (fsync_outgoing_stream_t const *)fvector_bsearch(pengine->outgoing_streams, &src, (fvector_comparer_t)fsync_outgoing_stream_cmp);
    if (psrc)
    {
        if (psrc->pistream)
            psrc->pistream->release(psrc->pistream);
        size_t item_idx = fvector_idx(pengine->outgoing_streams, psrc);
        fvector_erase(&pengine->outgoing_streams, item_idx);
    }
    fpop_lock();
}

static bool fsync_add_incoming_stream(fsync_engine_t *pengine, fsync_incoming_stream_t const *incoming_stream)
{
    bool ret = true;
    fpush_lock(pengine->incoming_streams_mutex);
    if (fvector_push_back(&pengine->incoming_streams, incoming_stream))
        fvector_qsort(pengine->incoming_streams, (fvector_comparer_t)fsync_incoming_stream_cmp);
    else
    {
        FS_ERR("No memory for new synchronization stream");
        ret = false;
    }
    fpop_lock();
    return ret;
}

static void fsync_remove_incoming_stream(fsync_engine_t *pengine, fuuid_t const *src, uint32_t sync_id)
{
    fsync_incoming_stream_t const dst =
    {
        sync_id,
        *src
    };

    fpush_lock(pengine->incoming_streams_mutex);
    fsync_incoming_stream_t const *pdst = (fsync_incoming_stream_t const *)fvector_bsearch(pengine->incoming_streams, &dst, (fvector_comparer_t)fsync_incoming_stream_cmp);
    if (pdst)
    {
        if (pdst->signature_ostream)
            pdst->signature_ostream->release(pdst->signature_ostream);
        size_t item_idx = fvector_idx(pengine->incoming_streams, pdst);
        fvector_erase(&pengine->incoming_streams, item_idx);
    }
    fpop_lock();
}

#if 0

// FSYNC_REQUEST handler
static void fsync_request_handler(fsync_engine_t *pengine, FMSG_TYPE(sync_request) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pengine->uuid, sizeof pengine->uuid) != 0)
        return;

    fsync_incoming_stream_t sync_dst =
    {
        msg->hdr.src,
        msg->listener_id,
        msg->sync_id,
        time(0)
    };

    char const *err_msg = "";

    ferr_t ret = fsync_add_dst(pengine, &sync_dst) ? FSUCCESS : FERR_NO_MEM;
    if (ret == FSUCCESS)
    {
        ret = frstream_factory_stream_request(pengine->stream_factory, &msg->hdr.src, msg->sync_id);
        if (ret != FSUCCESS)
        {
            FS_ERR(err_msg = "Remote stream request was failed");
            fsync_remove_dst(pengine, &sync_dst.src, sync_dst.listener_id, sync_dst.sync_id);
        }
    }
    else err_msg = "No memory for new synchronization stream";

    if (ret != FSUCCESS)
    {
        FMSG(sync_failed, err, pengine->uuid, msg->hdr.src,
            msg->listener_id,
            msg->sync_id,
            ret
        );
        strncpy(err.msg, err_msg, sizeof err.msg);

        if (fmsgbus_publish(pengine->msgbus, FSYNC_FAILED, (fmsg_t const *)&err) != FSUCCESS)
            FS_ERR("Synchronization error not published");
    }
}

// FSYNC_FAILED handler
static void fsync_failure_handler(fsync_engine_t *pengine, FMSG_TYPE(sync_failed) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pengine->uuid, sizeof pengine->uuid) != 0)
        return;
    fsync_remove_src(pengine, &msg->hdr.src, msg->listener_id, msg->sync_id);
    FS_ERR("Synchronization was failed. Reason: \'%s\'", msg->msg);
}
#endif

static void fsync_engine_istream_listener(fsync_engine_t *pengine, fistream_t *pstream, frstream_info_t const *info)
{
    // TODO
}

static void fsync_engine_msgbus_retain(fsync_engine_t *pengine, fmsgbus_t *pmsgbus)
{
    pengine->msgbus = fmsgbus_retain(pmsgbus);
//    fmsgbus_subscribe(pengine->msgbus, FSYNC_REQUEST,   (fmsg_handler_t)fsync_request_handler,  pengine);
//    fmsgbus_subscribe(pengine->msgbus, FSYNC_FAILED,    (fmsg_handler_t)fsync_failure_handler,  pengine);
}

static void fsync_engine_msgbus_release(fsync_engine_t *pengine)
{
//    fmsgbus_unsubscribe(pengine->msgbus, FSYNC_REQUEST, (fmsg_handler_t)fsync_request_handler);
//    fmsgbus_unsubscribe(pengine->msgbus, FSYNC_FAILED,  (fmsg_handler_t)fsync_failure_handler);
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

    pengine->listeners_mutex = PTHREAD_MUTEX_INITIALIZER;
    pengine->listeners = fvector(sizeof(fsync_listener_t*), 0, 0);
    if (!pengine->listeners)
    {
        FS_ERR("Listeners vector wasn't created");
        fsync_engine_release(pengine);
        return 0;
    }

    pengine->outgoing_streams_mutex = PTHREAD_MUTEX_INITIALIZER;
    pengine->outgoing_streams = fvector(sizeof(fsync_outgoing_stream_t), 0, 0);
    if (!pengine->outgoing_streams)
    {
        FS_ERR("Sources vector wasn't created");
        fsync_engine_release(pengine);
        return 0;
    }

    pengine->incoming_streams_mutex = PTHREAD_MUTEX_INITIALIZER;
    pengine->incoming_streams = fvector(sizeof(fsync_incoming_stream_t), 0, 0);
    if (!pengine->incoming_streams)
    {
        FS_ERR("Destinations vector wasn't created");
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
                frstream_factory_release(pengine->stream_factory);
            }

            if(pengine->listeners)
            {
                for(size_t i = 0; i < fvector_size(pengine->listeners); ++i)
                {
                    fsync_listener_t *listener = *(fsync_listener_t**)fvector_at(pengine->listeners, i);
                    listener->release(listener);
                }
                fvector_release(pengine->listeners);
            }

            if (pengine->outgoing_streams)
            {
                for(size_t i = 0; i < fvector_size(pengine->outgoing_streams); ++i)
                {
                    fsync_outgoing_stream_t *outgoing_stream = (fsync_outgoing_stream_t *)fvector_at(pengine->outgoing_streams, i);
                    outgoing_stream->pistream->release(outgoing_stream->pistream);
                }
                fvector_release(pengine->outgoing_streams);
            }

            if(pengine->incoming_streams)
            {
                for(size_t i = 0; i < fvector_size(pengine->incoming_streams); ++i)
                {
                    fsync_incoming_stream_t *incoming_stream = (fsync_incoming_stream_t *)fvector_at(pengine->incoming_streams, i);
                    incoming_stream->signature_ostream->release(incoming_stream->signature_ostream);
                }
                fvector_release(pengine->incoming_streams);
            }

            free(pengine);
        }
    }
    else
        FS_ERR("Invalid sync engine");
}

ferr_t fsync_engine_register_listener(fsync_engine_t *pengine, fsync_listener_t *listener)
{
    if (!pengine || !listener)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    listener->retain(listener);

    ferr_t ret = FSUCCESS;

    fpush_lock(pengine->listeners_mutex);
    if (fvector_push_back(&pengine->listeners, listener))
        fvector_qsort(pengine->listeners, (fvector_comparer_t)fsync_listeners_cmp);
    else
    {
        listener->release(listener);
        FS_ERR("No memory for new listener");
        ret = FERR_NO_MEM;
    }
    fpop_lock();

    return ret;
}

ferr_t fsync_engine_sync(fsync_engine_t *pengine, fuuid_t const *dst, uint32_t listener_id, binn *metainf, fistream_t *pstream)
{
    if (!pengine || !dst || !pstream)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    uint32_t const metainf_size = metainf ? binn_size(metainf) : 0;
    if (metainf_size > FMAX_METAINF_SIZE)
    {
        FS_ERR("User data size is too long");
        return 0;
    }

    fsync_outgoing_stream_t outgoing_stream =
    {
        ++pengine->sync_id,
        *dst,
        listener_id,
        time(0),
        pstream
    };

    ferr_t ret = fsync_add_outgoing_stream(pengine, &outgoing_stream) ? FSUCCESS : FERR_NO_MEM;
    if (ret != FSUCCESS)
        return ret;

    FMSG(sync_request, req, pengine->uuid, *dst,
        listener_id,
        outgoing_stream.sync_id,
        metainf_size
    );
    if (metainf) memcpy(req.metainf, binn_ptr(metainf), metainf_size);

    ret = fmsgbus_publish(pengine->msgbus, FSYNC_REQUEST, (fmsg_t const *)&req);
    if (ret != FSUCCESS)
        fsync_remove_outgoing_stream(pengine, outgoing_stream.sync_id);

    return ret;
}
