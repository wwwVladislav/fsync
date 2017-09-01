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
 *  src  ------- DATA ------------------------> dst
 *       -- FSYNC_REQUEST --------------------> accept  mandatory
 *      <-- signature istream/FSYNC_FAILED ---          mandatory
 *       -- FSYNC_CANCEL/FSYNC_START --------->         optional
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
    fistream_t             *signature_istream;
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
    fvector_t              *listeners;                  // vector of fsync_listener_t*

    pthread_mutex_t         outgoing_streams_mutex;
    fvector_t              *outgoing_streams;           // vector of fsync_outgoing_stream_t

    pthread_mutex_t         incoming_streams_mutex;
    fvector_t              *incoming_streams;           // vector of fsync_incoming_stream_t
};

static int fsync_listener_cmp(fsync_listener_t const **lhs, fsync_listener_t const **rhs)
{
    return (int)(*lhs)->id - (*rhs)->id;
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

static ferr_t fsync_listener_add(fsync_engine_t *pengine, fsync_listener_t *listener)
{
    ferr_t ret = FSUCCESS;

    listener->retain(listener);

    fpush_lock(pengine->listeners_mutex);
    if (fvector_push_back(&pengine->listeners, &listener))
        fvector_qsort(pengine->listeners, (fvector_comparer_t)fsync_listener_cmp);
    else
    {
        listener->release(listener);
        FS_ERR("No memory for new listener");
        ret = FERR_NO_MEM;
    }
    fpop_lock();

    return ret;
}

static fsync_listener_t *fsync_listener_get(fsync_engine_t *pengine, uint32_t listener_id)
{
    fsync_listener_t *listener = 0;
    fsync_listener_t const key = { listener_id };
    fsync_listener_t const *pkey = &key;

    fpush_lock(pengine->listeners_mutex);
    fsync_listener_t **plistener = (fsync_listener_t **)fvector_bsearch(pengine->listeners, &pkey, (fvector_comparer_t)fsync_listener_cmp);
    if (plistener)
        listener = (*plistener)->retain(*plistener);
    fpop_lock();

    return listener;
}

static bool fsync_outgoing_stream_add(fsync_engine_t *pengine, fsync_outgoing_stream_t const *outgoing_stream)
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

static void fsync_outgoing_stream_remove(fsync_engine_t *pengine, uint32_t sync_id)
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
        if (psrc->signature_istream)
            psrc->signature_istream->release(psrc->signature_istream);
        size_t item_idx = fvector_idx(pengine->outgoing_streams, psrc);
        fvector_erase(&pengine->outgoing_streams, item_idx);
    }
    fpop_lock();
}

static bool fsync_outgoing_stream_set_signature_istream(fsync_engine_t *pengine, uint32_t sync_id, fistream_t *signature_istream)
{
    bool ret = true;

    fsync_outgoing_stream_t const src =
    {
        sync_id
    };

    fpush_lock(pengine->outgoing_streams_mutex);
    fsync_outgoing_stream_t *psrc = (fsync_outgoing_stream_t *)fvector_bsearch(pengine->outgoing_streams, &src, (fvector_comparer_t)fsync_outgoing_stream_cmp);
    if (psrc)
    {
        if (!psrc->signature_istream)
            psrc->signature_istream = signature_istream->retain(signature_istream);
        else
            ret = false;
    }
    else ret = false;
    fpop_lock();

    return ret;
}

static bool fsync_incoming_stream_add(fsync_engine_t *pengine, fsync_incoming_stream_t const *incoming_stream)
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

static void fsync_incoming_stream_remove(fsync_engine_t *pengine, fuuid_t const *src, uint32_t sync_id)
{
    fsync_incoming_stream_t const incoming_stream =
    {
        sync_id,
        *src
    };

    fpush_lock(pengine->incoming_streams_mutex);
    fsync_incoming_stream_t const *pincoming_stream = (fsync_incoming_stream_t const *)fvector_bsearch(pengine->incoming_streams, &incoming_stream, (fvector_comparer_t)fsync_incoming_stream_cmp);
    if (pincoming_stream)
    {
        if (pincoming_stream->signature_ostream)
            pincoming_stream->signature_ostream->release(pincoming_stream->signature_ostream);
        size_t item_idx = fvector_idx(pengine->incoming_streams, pincoming_stream);
        fvector_erase(&pengine->incoming_streams, item_idx);
    }
    fpop_lock();
}

void fsync_incoming_stream_set_signature_ostream(fsync_engine_t *pengine, fuuid_t const *src, uint32_t sync_id, fostream_t *signature_ostream)
{
    fsync_incoming_stream_t const incoming_stream =
    {
        sync_id,
        *src
    };

    fpush_lock(pengine->incoming_streams_mutex);
    fsync_incoming_stream_t *pincoming_stream = (fsync_incoming_stream_t *)fvector_bsearch(pengine->incoming_streams, &incoming_stream, (fvector_comparer_t)fsync_incoming_stream_cmp);
    if (pincoming_stream)
        pincoming_stream->signature_ostream = signature_ostream->retain(signature_ostream);
    fpop_lock();
}

// FSYNC_REQUEST handler
static void fsync_request_handler(fsync_engine_t *pengine, FMSG_TYPE(sync_request) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pengine->uuid, sizeof pengine->uuid) != 0)
        return;

    ferr_t            ret     = FSUCCESS;
    char const       *err_msg = "";
    fsync_listener_t *listener = 0;
    binn             *metainf = msg->metainf_size ? binn_open((void *)msg->metainf) : 0;

    fsync_incoming_stream_t incoming_stream =
    {
        msg->sync_id,
        msg->hdr.src,
        msg->listener_id,
        time(0),
        0
    };

    do
    {
        // I. Find listener
        listener = fsync_listener_get(pengine, msg->listener_id);
        if (!listener)
        {
            ret = FFAIL;
            err_msg = "Synchronization listener isn't found";
            FS_ERR(err_msg);
            break;
        }

        // II. Accept the sync request
        if (!listener->accept(listener, metainf))
        {
            err_msg = "Sync request wasn't accepted";
            FS_ERR(err_msg);
            break;
        }

        // III. Remember the incoming stream
        ret = fsync_incoming_stream_add(pengine, &incoming_stream) ? FSUCCESS : FERR_NO_MEM;
        if (ret != FSUCCESS)
        {
            err_msg = "There is no free memory for incoming stream";
            FS_ERR(err_msg);
            break;
        }

        // IV. Request ostream for data signature
        binn *obj = binn_object();
        if (!obj)
        {
            err_msg = "Remote stream request was failed. Binn isn't created.";
            FS_ERR(err_msg);
            break;
        }

        binn_object_set_uint32(obj, "sync_id", msg->sync_id);

        fostream_t *signature_ostream = frstream_factory_stream(pengine->stream_factory, &msg->hdr.src, obj);

        binn_free(obj);

        if (!signature_ostream)
        {
            err_msg = "Remote stream request was failed";
            FS_ERR(err_msg);
            break;
        }

        fsync_incoming_stream_set_signature_ostream(pengine, &msg->hdr.src, msg->sync_id, signature_ostream);

        signature_ostream->release(signature_ostream);
    } while(0);

    if (metainf) binn_free(metainf);
    if (listener) listener->release(listener);

    if (ret != FSUCCESS)
    {
        fsync_incoming_stream_remove(pengine, &msg->hdr.src, msg->sync_id);

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
    fsync_outgoing_stream_remove(pengine, msg->sync_id);
    FS_ERR("Synchronization was failed. Reason: \'%s\'", msg->msg);
}

// FSYNC_CANCEL handler
static void fsync_cancel_handler(fsync_engine_t *pengine, FMSG_TYPE(sync_cancel) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pengine->uuid, sizeof pengine->uuid) != 0)
        return;
    fsync_incoming_stream_remove(pengine, &msg->hdr.src, msg->sync_id);
    FS_ERR("Synchronization was canceled. Reason: \'%s\'", msg->msg);
}

// FSYNC_START handler
static void fsync_start_handler(fsync_engine_t *pengine, FMSG_TYPE(sync_start) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pengine->uuid, sizeof pengine->uuid) != 0)
        return;

    char src_str[2 * sizeof(fuuid_t) + 1] = { 0 };
    char dst_str[2 * sizeof(fuuid_t) + 1] = { 0 };
    FS_INFO("Synchronization start: %s->%s",
            fuuid2str(&msg->hdr.src, src_str, sizeof src_str),
            fuuid2str(&msg->hdr.dst, dst_str, sizeof dst_str));

    // TODO
}

static void fsync_engine_istream_listener(fsync_engine_t *pengine, fistream_t *pstream, frstream_info_t const *info)
{
    ferr_t      ret = FSUCCESS;
    char const *err_msg = "";
    uint32_t    sync_id = 0;

    do
    {
        if (!info->metainf)
        {
            FS_ERR("Invalid input stream meta information");
            break;
        }

        sync_id = binn_object_uint32(info->metainf, "sync_id");

        if (!fsync_outgoing_stream_set_signature_istream(pengine, sync_id, pstream))
        {
            ret = FFAIL;
            err_msg = "Unknown synchronization id";
            FS_ERR(err_msg);
        }
    } while(0);

    if (ret == FSUCCESS)
    {
        FMSG(sync_start, start, pengine->uuid, info->peer,
            sync_id
        );
        if (fmsgbus_publish(pengine->msgbus, FSYNC_START, (fmsg_t const *)&start) != FSUCCESS)
            FS_ERR("Synchronization start message wasn't published");
    }
    else
    {
        FMSG(sync_cancel, err, pengine->uuid, info->peer,
            sync_id,
            ret
        );
        strncpy(err.msg, err_msg, sizeof err.msg);
        if (fmsgbus_publish(pengine->msgbus, FSYNC_CANCEL, (fmsg_t const *)&err) != FSUCCESS)
            FS_ERR("Synchronization error wasn't published");
    }
}

static void fsync_engine_msgbus_retain(fsync_engine_t *pengine, fmsgbus_t *pmsgbus)
{
    pengine->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(pengine->msgbus, FSYNC_REQUEST,   (fmsg_handler_t)fsync_request_handler,  pengine);
    fmsgbus_subscribe(pengine->msgbus, FSYNC_FAILED,    (fmsg_handler_t)fsync_failure_handler,  pengine);
    fmsgbus_subscribe(pengine->msgbus, FSYNC_CANCEL,    (fmsg_handler_t)fsync_cancel_handler,   pengine);
    fmsgbus_subscribe(pengine->msgbus, FSYNC_START,     (fmsg_handler_t)fsync_start_handler,    pengine);
}

static void fsync_engine_msgbus_release(fsync_engine_t *pengine)
{
    fmsgbus_unsubscribe(pengine->msgbus, FSYNC_REQUEST, (fmsg_handler_t)fsync_request_handler);
    fmsgbus_unsubscribe(pengine->msgbus, FSYNC_FAILED,  (fmsg_handler_t)fsync_failure_handler);
    fmsgbus_unsubscribe(pengine->msgbus, FSYNC_CANCEL,  (fmsg_handler_t)fsync_cancel_handler);
    fmsgbus_unsubscribe(pengine->msgbus, FSYNC_START,   (fmsg_handler_t)fsync_start_handler);
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

    return fsync_listener_add(pengine, listener);
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

    ferr_t ret = fsync_outgoing_stream_add(pengine, &outgoing_stream) ? FSUCCESS : FERR_NO_MEM;
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
        fsync_outgoing_stream_remove(pengine, outgoing_stream.sync_id);

    return ret;
}
