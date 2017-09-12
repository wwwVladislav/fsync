#include "sync_engine.h"
#include "rstream.h"
#include "rsync.h"
#include <futils/log.h>
#include <futils/utils.h>
#include <futils/vector.h>
#include <futils/mutex.h>
#include <fcommon/limits.h>
#include <fcommon/messages.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

/*
 *        synchronization
 *  src  ------- DATA ------------------------> dst
 *       -- FSYNC_REQUEST -------------------->         mandatory   FSYNC_STATE_INIT
 *      <-- signature istream/FSYNC_FAILED ---          mandatory
 *       -- FSYNC_CANCEL --------------------->         optional
 *      <-- [signature] ----------------------          mandatory
 *       -- delta istream -------------------->         mandatory
 *       ---- [delta] ------------------------> apply   mandatory
*/

// src
typedef struct
{
    uint32_t                sync_id;                            // synchronization id (key)
    fuuid_t                 dst;                                // destination uuid
    uint32_t                agent_id;                           // synchronization agent id
    time_t                  time;                               // synchronization start time
    binn                   *metainf;                            // meta information
    fistream_t             *pistream;                           // istream with data
    fostream_t             *delta_ostream;                      // ostream for delta
} fsync_src_t;

// dst
typedef struct
{
    uint32_t                sync_id;                            // synchronization id (key)
    fuuid_t                 src;                                // source uuid (key)
    uint32_t                agent_id;                           // synchronization agent id
    time_t                  time;                               // synchronization start time
    binn                   *metainf;                            // meta information
    fostream_t             *signature_ostream;                  // ostream for signature
    fistream_t             *pistream;                           // istream with data
    fostream_t             *postream;                           // ostream for data
} fsync_dst_t;

typedef struct
{
    volatile bool           is_active;                          // thread activity flag
    volatile bool           is_busy;                            // synchronization flag (true - if thread busy with synchronization)
    sem_t                   sem;                                // Semaphore for events wait
    pthread_t               thread;                             // synchronization thread
} fsync_thread_t;

typedef struct
{
    fsync_thread_t          sync;                               // synchronization thread status
    volatile uint32_t       sync_id;                            // synchronization id
    fistream_t             *signature_istream;                  // signature istream
} fsync_src_thread_t;

typedef struct
{
    fsync_thread_t          sync;
    uint32_t                sync_id;                            // synchronization id
    fuuid_t                 src;                                // source uuid
    fistream_t             *delta_istream;                      // delta istream
} fsync_dst_thread_t;

typedef struct
{
    pthread_mutex_t         mutex;                              // mutex for streams vector guard
    fvector_t              *scrs;                               // vector of fsync_src_t
    sem_t                   sem;                                // Semaphore for stream data wait
    fsync_src_thread_t      threads[FDATA_SYNC_THREADS_NUM];    // synchronization threads
} fsync_src_threads_t;

typedef struct
{
    pthread_mutex_t         mutex;                              // mutex for streams vector guard
    fvector_t              *dsts;                               // vector of fsync_dst_t
    sem_t                   sem;                                // Semaphore for stream data wait
    fsync_dst_thread_t      threads[FDATA_SYNC_THREADS_NUM];    // synchronization threads
} fsync_dst_threads_t;

typedef struct
{
    fsync_engine_t         *pengine;
    uint32_t                thread_id;
} fsync_thread_param_t;

struct sync_engine
{
    volatile uint32_t       ref_counter;                        // references counter
    volatile uint32_t       sync_id;                            // synchronization id generator
    fuuid_t                 uuid;                               // current node uuid
    fmsgbus_t              *msgbus;                             // messages bus
    frstream_factory_t     *stream_factory;                     // remote streams factory

    pthread_mutex_t         agents_mutex;                       // agents vector guard mutex
    fvector_t              *agents;                             // vector of fsync_agent_t*

    fsync_src_threads_t     src_threads;                        // src threads
    fsync_dst_threads_t     dst_threads;                        // dst threads
};

typedef enum
{
    FSYNC_SIGNATURE_STREAM = 0,
    FSYNC_DELTA_STREAM
} fsync_stream_t;

typedef struct
{
    uint32_t        sync_id;
    fsync_stream_t  stream_type;
} fsync_stream_metainf_t;

static binn *fsync_signature_stream_metainf(uint32_t sync_id)
{
    binn *obj = binn_object();
    if (obj)
    {
        binn_object_set_uint32(obj, "sync_id",     sync_id);
        binn_object_set_uint8 (obj, "stream_type", FSYNC_SIGNATURE_STREAM);
    }
    return obj;
}

static binn *fsync_delta_stream_metainf(uint32_t sync_id)
{
    binn *obj = binn_object();
    if (obj)
    {
        binn_object_set_uint32(obj, "sync_id",     sync_id);
        binn_object_set_uint8 (obj, "stream_type", FSYNC_DELTA_STREAM);
    }
    return obj;
}

static fsync_stream_metainf_t fsync_stream_metainf(binn *metainf)
{
    fsync_stream_metainf_t ret =
    {
        binn_object_uint32(metainf, "sync_id"),
        binn_object_uint8 (metainf, "stream_type")
    };
    return ret;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// fsync_agent
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

static int fsync_agent_cmp(fsync_agent_t const **lhs, fsync_agent_t const **rhs)
{
    return (int)(*lhs)->id - (*rhs)->id;
}

static ferr_t fsync_agent_add(fsync_engine_t *pengine, fsync_agent_t *agent)
{
    ferr_t ret = FSUCCESS;

    agent->retain(agent);

    fpush_lock(pengine->agents_mutex);
    if (fvector_push_back(&pengine->agents, &agent))
        fvector_qsort(pengine->agents, (fvector_comparer_t)fsync_agent_cmp);
    else
    {
        agent->release(agent);
        FS_ERR("No memory for new agent");
        ret = FERR_NO_MEM;
    }
    fpop_lock();

    return ret;
}

static fsync_agent_t *fsync_agent_get(fsync_engine_t *pengine, uint32_t agent_id)
{
    fsync_agent_t *agent = 0;
    fsync_agent_t const key = { agent_id };
    fsync_agent_t const *pkey = &key;

    fpush_lock(pengine->agents_mutex);
    fsync_agent_t **pagent = (fsync_agent_t **)fvector_bsearch(pengine->agents, &pkey, (fvector_comparer_t)fsync_agent_cmp);
    if (pagent)
        agent = (*pagent)->retain(*pagent);
    fpop_lock();

    return agent;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// fsync_src
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

static bool fsync_src_push_back(fsync_src_threads_t *src_threads, fsync_src_t const *src)
{
    src->pistream->retain(src->pistream);

    bool is_added = true;

    fpush_lock(src_threads->mutex);
    if (!fvector_push_back(&src_threads->scrs, src))
    {
        src->pistream->release(src->pistream);
        FS_ERR("No memory for new synchronization stream");
        is_added = false;
    }
    fpop_lock();

    if (is_added)
        sem_post(&src_threads->sem);

    return is_added;
}

static bool fsync_src_pop_back(fsync_src_threads_t *src_threads, fsync_src_t *src)
{
    bool ret = false;

    fpush_lock(src_threads->mutex);
    size_t vector_size = fvector_size(src_threads->scrs);
    if (vector_size)
    {
        fsync_src_t *last = (fsync_src_t *)fvector_at(src_threads->scrs, vector_size - 1);
        *src = *last;
        fvector_erase(&src_threads->scrs, vector_size - 1);
        ret = true;
    }
    fpop_lock();

    return ret;
}

static void fsync_src_free(fsync_src_t *src)
{
    if (src->pistream)
        src->pistream->release(src->pistream);
    if (src->delta_ostream)
        src->delta_ostream->release(src->delta_ostream);
    binn_free(src->metainf);
}

static bool fsync_src_remove_last(fsync_src_threads_t *src_threads)
{
    bool ret = false;

    fpush_lock(src_threads->mutex);
    size_t vector_size = fvector_size(src_threads->scrs);
    if (vector_size)
    {
        fsync_src_t *poutgoing_stream = (fsync_src_t *)fvector_at(src_threads->scrs, vector_size - 1);
        fsync_src_free(poutgoing_stream);
        fvector_erase(&src_threads->scrs, vector_size - 1);
        ret = true;
    }
    fpop_lock();

    return ret;
}

static void *fsync_src_thread(void *);

static bool fsync_src_create(fsync_engine_t *pengine, fsync_src_threads_t *src_threads)
{
    src_threads->mutex = PTHREAD_MUTEX_INITIALIZER;

    src_threads->scrs = fvector(sizeof(fsync_src_t), 0, 0);
    if (!src_threads->scrs)
    {
        FS_ERR("Streams vector wasn't created");
        return false;
    }

    if (sem_init(&src_threads->sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        return false;
    }

    for(uint32_t i = 0; i < FARRAY_SIZE(src_threads->threads); ++i)
    {
        fsync_src_thread_t *sync_thread = src_threads->threads + i;
        sync_thread->sync.is_active = false;
        sync_thread->sync.is_busy = false;

        if (sem_init(&sync_thread->sync.sem, 0, 0) == -1)
        {
            FS_ERR("The semaphore initialization is failed");
            return false;
        }

        fsync_thread_param_t thread_param = { pengine, i };

        int rc = pthread_create(&sync_thread->sync.thread, 0, fsync_src_thread, &thread_param);
        if (rc)
        {
            FS_ERR("Unable to create the thread. Error: %d", rc);
            return false;
        }

        static struct timespec const ts = { 1, 0 };
        while(!sync_thread->sync.is_active)
            nanosleep(&ts, NULL);
    }

    return true;
}

static void fsync_src_threads_free(fsync_src_threads_t *src_threads)
{
    for(uint32_t i = 0; i < FARRAY_SIZE(src_threads->threads); ++i)
        src_threads->threads[i].sync.is_active = false;

    for(uint32_t i = 0; i < FARRAY_SIZE(src_threads->threads); ++i)
    {
        sem_post(&src_threads->sem);
        sem_post(&src_threads->threads[i].sync.sem);
    }

    for(uint32_t i = 0; i < FARRAY_SIZE(src_threads->threads); ++i)
    {
        fsync_src_thread_t *thread = src_threads->threads + i;
        pthread_join(thread->sync.thread, 0);
        sem_destroy(&thread->sync.sem);
        if (thread->signature_istream)
            thread->signature_istream->release(thread->signature_istream);
        thread->signature_istream = 0;
    }

    sem_destroy(&src_threads->sem);

    if (src_threads->scrs)
    {
        while(fsync_src_remove_last(src_threads));
        fvector_release(src_threads->scrs);
    }
}

static void *fsync_src_thread(void *param)
{
    fsync_thread_param_t *thread_param = (fsync_thread_param_t *)param;
    fsync_engine_t       *pengine      = thread_param->pengine;
    uint32_t const        thread_id    = thread_param->thread_id;
    fsync_src_threads_t  *src_threads  = &pengine->src_threads;
    fsync_src_thread_t   *thread       = src_threads->threads + thread_id;

    thread->sync.is_active = true;

    while(thread->sync.is_active)
    {
        while (sem_wait(&src_threads->sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler
        if (!thread->sync.is_active)
            break;

        ferr_t ret = FSUCCESS;
        char const *err_msg = "";

        fsync_src_t                src = { 0 };
        fsync_agent_t             *agent = 0;
        frsync_signature_t        *psig = 0;
        frsync_delta_calculator_t *pdelta_calc = 0;

        if (!fsync_src_pop_back(src_threads, &src))
            continue;

        thread->sync_id = src.sync_id;
        thread->sync.is_busy = true;

        do
        {
            agent = fsync_agent_get(pengine, src.agent_id);
            if (!agent)
            {
                ret = FFAIL;
                err_msg = "Synchronization request was performed to unknown agent";
                FS_ERR(err_msg);
                break;
            }

            // I. synchronization request
            FMSG(sync_request, req, pengine->uuid, src.dst,
                 src.agent_id,
                 src.sync_id,
                 src.metainf ? binn_size(src.metainf) : 0
            );
            if (src.metainf)
                memcpy(req.metainf, binn_ptr(src.metainf), req.metainf_size);

            ret = fmsgbus_publish(pengine->msgbus, FSYNC_REQUEST, (fmsg_t const *)&req);
            if (ret != FSUCCESS)
            {
                err_msg = "Synchronization request publishing was failed";
                FS_ERR(err_msg);
                if (agent->failed)
                    agent->failed(agent, src.metainf, ret, err_msg);
                break;
            }

            // II. Wait signature istream
            while (sem_wait(&thread->sync.sem) == -1 && errno == EINTR)
                continue;       // Restart if interrupted by handler
            if (!thread->sync.is_active || !thread->sync.is_busy)
                break;

            // III. Load signature
            psig = frsync_signature_create();
            if (!psig)
            {
                ret = FFAIL;
                err_msg = "Signature creation was failed";
                FS_ERR(err_msg);
                break;
            }

            ret = frsync_signature_load(psig, thread->signature_istream);
            if (ret != FSUCCESS)
            {
                err_msg = "Signature receiving was failed";
                FS_ERR(err_msg);
                break;
            }

            char str[2 * sizeof(fuuid_t) + 1] = { 0 };
            FS_INFO("Signature received: %s", fuuid2str(&pengine->uuid, str, sizeof str));

            // IV. Request ostream for data signature
            binn *obj = fsync_delta_stream_metainf(src.sync_id);
            if (!obj)
            {
                ret = FFAIL;
                err_msg = "Remote stream request was failed. Binn isn't created.";
                FS_ERR(err_msg);
                break;
            }
            src.delta_ostream = frstream_factory_stream(pengine->stream_factory, &src.dst, obj);
            binn_free(obj);

            if (!src.delta_ostream)
            {
                ret = FFAIL;
                err_msg = "Remote stream request was failed";
                FS_ERR(err_msg);
                break;
            }

            // V. Calculate delta
            pdelta_calc = frsync_delta_calculator_create(psig);
            if (!pdelta_calc)
            {
                ret = FFAIL;
                err_msg = "Delta calculation was failed";
                FS_ERR(err_msg);
                break;
            }

            ret = frsync_delta_calculate(pdelta_calc,
                                         src.pistream,
                                         src.delta_ostream);
            if (ret != FSUCCESS)
            {
                err_msg = "Delta calculation was failed";
                FS_ERR(err_msg);
                break;
            }
        }
        while(0);

        if(psig)
            frsync_signature_release(psig);
        if (pdelta_calc)
            frsync_delta_calculator_release(pdelta_calc);
        if (thread->signature_istream)
        {
            thread->signature_istream->release(thread->signature_istream);
            thread->signature_istream = 0;
        }
        thread->sync_id = 0;
        thread->sync.is_busy = false;

        if (agent) agent->release(agent);
        fsync_src_free(&src);

        if (ret != FSUCCESS)
        {
            FMSG(sync_cancel, err, pengine->uuid, src.dst,
                src.sync_id,
                ret
            );
            strncpy(err.msg, err_msg, sizeof err.msg);
            if (fmsgbus_publish(pengine->msgbus, FSYNC_CANCEL, (fmsg_t const *)&err) != FSUCCESS)
                FS_ERR("Synchronization error wasn't published");
        }
    }
    return 0;
}

static bool fsync_src_set_signature_istream(fsync_src_threads_t *src_threads, uint32_t sync_id, fistream_t *signature_istream)
{
    for(uint32_t i = 0; i < FARRAY_SIZE(src_threads->threads); ++i)
    {
        fsync_src_thread_t *src_thread = src_threads->threads + i;
        if (src_thread->sync_id == sync_id)
        {
            if (src_thread->signature_istream)
            {
                FS_ERR("Signature istream is already exist");
                return false;
            }
            src_thread->signature_istream = signature_istream->retain(signature_istream);
            sem_post(&src_thread->sync.sem);
            return true;
        }
    }
    return false;
}

static void fsync_src_cancel(fsync_src_threads_t *src_threads, uint32_t sync_id)
{
    for(uint32_t i = 0; i < FARRAY_SIZE(src_threads->threads); ++i)
    {
        fsync_src_thread_t *src_thread = src_threads->threads + i;
        if (src_thread->sync_id == sync_id)
        {
            src_thread->sync.is_busy = false;
            sem_post(&src_thread->sync.sem);
            break;
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// fsync_dst
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

static bool fsync_dst_push_back(fsync_dst_threads_t *dst_threads, fsync_dst_t const *dst)
{
    bool is_added = true;

    fpush_lock(dst_threads->mutex);
    if (!fvector_push_back(&dst_threads->dsts, dst))
    {
        FS_ERR("No memory for new synchronization stream");
        is_added = false;
    }
    fpop_lock();

    if (is_added)
        sem_post(&dst_threads->sem);

    return is_added;
}

static bool fsync_dst_pop_back(fsync_dst_threads_t *dst_threads, fsync_dst_t *dst)
{
    bool ret = false;

    fpush_lock(dst_threads->mutex);
    size_t vector_size = fvector_size(dst_threads->dsts);
    if (vector_size)
    {
        fsync_dst_t *last = (fsync_dst_t *)fvector_at(dst_threads->dsts, vector_size - 1);
        *dst = *last;
        fvector_erase(&dst_threads->dsts, vector_size - 1);
        ret = true;
    }
    fpop_lock();

    return ret;
}

static void fsync_dst_free(fsync_dst_t *dst)
{
    if (dst->signature_ostream)
        dst->signature_ostream->release(dst->signature_ostream);
    if (dst->pistream)
        dst->pistream->release(dst->pistream);
    if (dst->postream)
        dst->postream->release(dst->postream);
    binn_free(dst->metainf);
}

static bool fsync_dst_remove_last(fsync_dst_threads_t *dst_threads)
{
    bool ret = false;

    fpush_lock(dst_threads->mutex);
    size_t vector_size = fvector_size(dst_threads->dsts);
    if (vector_size)
    {
        fsync_dst_t *pincoming_stream = (fsync_dst_t *)fvector_at(dst_threads->dsts, vector_size - 1);
        fsync_dst_free(pincoming_stream);
        fvector_erase(&dst_threads->dsts, vector_size - 1);
        ret = true;
    }
    fpop_lock();

    return ret;
}

static void *fsync_dst_thread(void *);

static bool fsync_dst_create(fsync_engine_t *pengine, fsync_dst_threads_t *dst_threads)
{
    dst_threads->mutex = PTHREAD_MUTEX_INITIALIZER;

    dst_threads->dsts = fvector(sizeof(fsync_dst_t), 0, 0);
    if (!dst_threads->dsts)
    {
        FS_ERR("Streams vector wasn't created");
        return false;
    }

    if (sem_init(&dst_threads->sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        return false;
    }

    for(uint32_t i = 0; i < FARRAY_SIZE(dst_threads->threads); ++i)
    {
        fsync_dst_thread_t *sync_thread = dst_threads->threads + i;
        sync_thread->sync.is_active = false;
        sync_thread->sync.is_busy = false;

        if (sem_init(&sync_thread->sync.sem, 0, 0) == -1)
        {
            FS_ERR("The semaphore initialization is failed");
            return false;
        }

        fsync_thread_param_t thread_param = { pengine, i };

        int rc = pthread_create(&sync_thread->sync.thread, 0, fsync_dst_thread, &thread_param);
        if (rc)
        {
            FS_ERR("Unable to create the thread. Error: %d", rc);
            return false;
        }

        static struct timespec const ts = { 1, 0 };
        while(!sync_thread->sync.is_active)
            nanosleep(&ts, NULL);
    }

    return true;
}

static void fsync_dst_threads_free(fsync_dst_threads_t *dst_threads)
{
    for(uint32_t i = 0; i < FARRAY_SIZE(dst_threads->threads); ++i)
        dst_threads->threads[i].sync.is_active = false;

    for(uint32_t i = 0; i < FARRAY_SIZE(dst_threads->threads); ++i)
    {
        sem_post(&dst_threads->sem);
        sem_post(&dst_threads->threads[i].sync.sem);
    }

    for(uint32_t i = 0; i < FARRAY_SIZE(dst_threads->threads); ++i)
    {
        fsync_dst_thread_t *thread = dst_threads->threads + i;
        pthread_join(thread->sync.thread, 0);
        sem_destroy(&thread->sync.sem);
        if (thread->delta_istream)
            thread->delta_istream->release(thread->delta_istream);
        thread->delta_istream = 0;
    }

    sem_destroy(&dst_threads->sem);

    if (dst_threads->dsts)
    {
        while(fsync_dst_remove_last(dst_threads));
        fvector_release(dst_threads->dsts);
    }
}

static void *fsync_dst_thread(void *param)
{
    fsync_thread_param_t *thread_param = (fsync_thread_param_t *)param;
    fsync_engine_t       *pengine      = thread_param->pengine;
    uint32_t const        thread_id    = thread_param->thread_id;
    fsync_dst_threads_t  *dst_threads  = &pengine->dst_threads;
    fsync_dst_thread_t   *thread       = dst_threads->threads + thread_id;

    thread->sync.is_active = true;

    while(thread->sync.is_active)
    {
        while (sem_wait(&dst_threads->sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        fsync_dst_t                    dst = { 0 };
        fsync_agent_t                 *agent = 0;
        frsync_signature_calculator_t *psig_calc = 0;
        frsync_delta_t                *pdelta = 0;

        if (!fsync_dst_pop_back(dst_threads, &dst))
            continue;

        thread->sync_id = dst.sync_id;
        thread->src = dst.src;
        thread->sync.is_busy = true;

        ferr_t          ret     = FSUCCESS;
        char const     *err_msg = "";

        do
        {
            agent = fsync_agent_get(pengine, dst.agent_id);
            if (!agent)
            {
                FS_ERR("Synchronization request was performed to unknown agent");
                break;
            }

            // I. Accept the sync request
            if (!agent->accept(agent, dst.metainf, &dst.pistream, &dst.postream))
            {
                ret = FFAIL;
                err_msg = "Sync request wasn't accepted";
                FS_ERR(err_msg);
                break;
            }

            if (!dst.pistream)
            {
                ret = FFAIL;
                err_msg = "Destination istream is inaccessible";
                FS_ERR(err_msg);
                break;
            }

            if (!dst.postream)
            {
                ret = FFAIL;
                err_msg = "Destination ostream is inaccessible";
                FS_ERR(err_msg);
                break;
            }

            // II. Request ostream for data signature
            binn *obj = fsync_signature_stream_metainf(dst.sync_id);
            if (!obj)
            {
                ret = FFAIL;
                err_msg = "Remote stream request was failed. Binn isn't created.";
                FS_ERR(err_msg);
                break;
            }
            dst.signature_ostream = frstream_factory_stream(pengine->stream_factory, &dst.src, obj);
            binn_free(obj);

            if (!dst.signature_ostream)
            {
                ret = FFAIL;
                err_msg = "Remote stream request was failed";
                FS_ERR(err_msg);
                break;
            }

            if (!thread->sync.is_active || !thread->sync.is_busy)
                break;

            // III. Signature calculation
            psig_calc = frsync_signature_calculator_create();
            if (!psig_calc)
            {
                ret = FFAIL;
                err_msg = "Signature calculator wasn't created";
                FS_ERR(err_msg);
                break;
            }

            char dst_str[2 * sizeof(fuuid_t) + 1] = { 0 };
            FS_INFO("Calculate signature: %s", fuuid2str(&pengine->uuid, dst_str, sizeof dst_str));

            ret = frsync_signature_calculate(psig_calc,
                                             dst.pistream,
                                             dst.signature_ostream);
            if (ret != FSUCCESS)
            {
                err_msg = "Signature calculation was failed";
                FS_ERR(err_msg);
                break;
            }
            dst.signature_ostream->release(dst.signature_ostream);
            dst.signature_ostream = 0;

            if (!thread->sync.is_active || !thread->sync.is_busy)
                break;

            // IV. Wait delta istream
            while (sem_wait(&thread->sync.sem) == -1 && errno == EINTR)
                continue;       // Restart if interrupted by handler

            if (!thread->sync.is_active || !thread->sync.is_busy)
                break;

            // V. Delta apply
            pdelta = frsync_delta_create(dst.pistream);
            if (!pdelta)
            {
                ret = FFAIL;
                err_msg = "Delta wasn't created";
                FS_ERR(err_msg);
                break;
            }

            ret = frsync_delta_apply(pdelta,
                                     thread->delta_istream,
                                     dst.postream);
            if (ret != FSUCCESS)
            {
                err_msg = "Application of delta was failed";
                FS_ERR(err_msg);
                break;
            }

            FS_INFO("Delta was successfully applied: %s", fuuid2str(&pengine->uuid, dst_str, sizeof dst_str));
        }
        while(0);

        thread->sync_id = 0;

        if (pdelta)
            frsync_delta_release(pdelta);

        if (psig_calc)
            frsync_signature_calculator_release(psig_calc);

        if (agent)
            agent->release(agent);

        fsync_dst_free(&dst);

        thread->sync.is_busy = false;

        if (ret != FSUCCESS)
        {
            FMSG(sync_failed, err, pengine->uuid, dst.src,
                dst.sync_id,
                ret
            );
            strncpy(err.msg, err_msg, sizeof err.msg);
            if (fmsgbus_publish(pengine->msgbus, FSYNC_FAILED, (fmsg_t const *)&err) != FSUCCESS)
                FS_ERR("Synchronization error not published");
        }
    }
    return 0;
}

static bool fsync_dst_set_delta_istream(fsync_dst_threads_t *dst_threads, fuuid_t const *src, uint32_t sync_id, fistream_t *delta_istream)
{
    for(uint32_t i = 0; i < FARRAY_SIZE(dst_threads->threads); ++i)
    {
        fsync_dst_thread_t *dst_thread = dst_threads->threads + i;
        if (dst_thread->sync_id == sync_id
            && memcmp(&dst_thread->src, src, sizeof *src) == 0)
        {
            if (dst_thread->delta_istream)
            {
                FS_ERR("Delta istream is already exist");
                return false;
            }
            dst_thread->delta_istream = delta_istream->retain(delta_istream);
            sem_post(&dst_thread->sync.sem);
            return true;
        }
    }
    return false;
}

static void fsync_dst_cancel(fsync_dst_threads_t *dst_threads, fuuid_t const *src, uint32_t sync_id)
{
    for(uint32_t i = 0; i < FARRAY_SIZE(dst_threads->threads); ++i)
    {
        fsync_dst_thread_t *dst_thread = dst_threads->threads + i;
        if (dst_thread->sync_id == sync_id
            && memcmp(&dst_thread->src, src, sizeof *src) == 0)
        {
            dst_thread->sync.is_busy = false;
            sem_post(&dst_thread->sync.sem);
            return true;
        }
    }    
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// fsync_engine
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// FSYNC_REQUEST handler
static void fsync_request_handler(fsync_engine_t *pengine, FMSG_TYPE(sync_request) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pengine->uuid, sizeof pengine->uuid) != 0)
        return;

    fsync_dst_t dst =
    {
        msg->sync_id,
        msg->hdr.src,
        msg->agent_id,
        time(0),
        msg->metainf_size ? binn_open((void*)msg->metainf) : 0,
    };

    if (!fsync_dst_push_back(&pengine->dst_threads, &dst))
    {
        FMSG(sync_failed, err, pengine->uuid, msg->hdr.src,
            msg->sync_id,
            FFAIL,
            "There is no free memory for incoming stream"
        );

        if (fmsgbus_publish(pengine->msgbus, FSYNC_FAILED, (fmsg_t const *)&err) != FSUCCESS)
            FS_ERR("Synchronization error not published");
    }
}

// FSYNC_FAILED handler
static void fsync_failure_handler(fsync_engine_t *pengine, FMSG_TYPE(sync_failed) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pengine->uuid, sizeof pengine->uuid) != 0)
        return;
    fsync_src_cancel(&pengine->src_threads, msg->sync_id);
    FS_ERR("Synchronization was failed. Reason: \'%s\'", msg->msg);
}

// FSYNC_CANCEL handler
static void fsync_cancel_handler(fsync_engine_t *pengine, FMSG_TYPE(sync_cancel) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pengine->uuid, sizeof pengine->uuid) != 0)
        return;
    fsync_dst_cancel(&pengine->dst_threads, &msg->hdr.src, msg->sync_id);
    FS_ERR("Synchronization was canceled. Reason: \'%s\'", msg->msg);
}

static void fsync_engine_istream_listener(fsync_engine_t *pengine, fistream_t *pstream, frstream_info_t const *info)
{
    ferr_t      ret = FSUCCESS;
    char const *err_msg = "";
    uint32_t    sync_id = 0;

    if (!info->metainf)
    {
        FS_ERR("Invalid input stream meta information");
        return;
    }

    fsync_stream_metainf_t const metainf = fsync_stream_metainf(info->metainf);

    do
    {
        if(metainf.stream_type == FSYNC_SIGNATURE_STREAM)
        {
            if (!fsync_src_set_signature_istream(&pengine->src_threads, metainf.sync_id, pstream))
            {
                ret = FFAIL;
                err_msg = "Unknown synchronization id";
                FS_ERR(err_msg);
                break;
            }
        }
        else if(metainf.stream_type == FSYNC_DELTA_STREAM)
        {
            if (!fsync_dst_set_delta_istream(&pengine->dst_threads, &info->peer, metainf.sync_id, pstream))
            {
                ret = FFAIL;
                err_msg = "Unknown synchronization id";
                FS_ERR(err_msg);
                break;
            }
        }
        else
        {
            ret = FFAIL;
            err_msg = "Stream type is unsupported";
            FS_ERR(err_msg);
            break;
        }
    } while(0);

    if (ret != FSUCCESS)
    {
        if(metainf.stream_type == FSYNC_SIGNATURE_STREAM)
        {
            fsync_src_cancel(&pengine->src_threads, sync_id);

            FMSG(sync_cancel, err, pengine->uuid, info->peer,
                metainf.sync_id,
                ret
            );
            strncpy(err.msg, err_msg, sizeof err.msg);
            if (fmsgbus_publish(pengine->msgbus, FSYNC_CANCEL, (fmsg_t const *)&err) != FSUCCESS)
                FS_ERR("Synchronization error wasn't published");
        }
        else if(metainf.stream_type == FSYNC_DELTA_STREAM)
        {
            fsync_dst_cancel(&pengine->dst_threads, &info->peer, metainf.sync_id);

            FMSG(sync_failed, err, pengine->uuid, info->peer,
                metainf.sync_id,
                ret
            );
            strncpy(err.msg, err_msg, sizeof err.msg);
            if (fmsgbus_publish(pengine->msgbus, FSYNC_FAILED, (fmsg_t const *)&err) != FSUCCESS)
                FS_ERR("Synchronization error wasn't published");
        }
        else
            FS_ERR("Stream type is unsupported");
    }
}

static void fsync_engine_msgbus_retain(fsync_engine_t *pengine, fmsgbus_t *pmsgbus)
{
    pengine->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(pengine->msgbus, FSYNC_REQUEST,   (fmsg_handler_t)fsync_request_handler,  pengine);
    fmsgbus_subscribe(pengine->msgbus, FSYNC_FAILED,    (fmsg_handler_t)fsync_failure_handler,  pengine);
    fmsgbus_subscribe(pengine->msgbus, FSYNC_CANCEL,    (fmsg_handler_t)fsync_cancel_handler,   pengine);
}

static void fsync_engine_msgbus_release(fsync_engine_t *pengine)
{
    if (pengine->msgbus)
    {
        fmsgbus_unsubscribe(pengine->msgbus, FSYNC_REQUEST, (fmsg_handler_t)fsync_request_handler);
        fmsgbus_unsubscribe(pengine->msgbus, FSYNC_FAILED,  (fmsg_handler_t)fsync_failure_handler);
        fmsgbus_unsubscribe(pengine->msgbus, FSYNC_CANCEL,  (fmsg_handler_t)fsync_cancel_handler);
        fmsgbus_release(pengine->msgbus);
    }
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

    pengine->agents_mutex = PTHREAD_MUTEX_INITIALIZER;
    pengine->agents = fvector(sizeof(fsync_agent_t*), 0, 0);
    if (!pengine->agents)
    {
        FS_ERR("Agents vector wasn't created");
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

    if (!fsync_src_create(pengine, &pengine->src_threads))
    {
        FS_ERR("Sources vector wasn't created");
        fsync_engine_release(pengine);
        return 0;
    }

    if (!fsync_dst_create(pengine, &pengine->dst_threads))
    {
        FS_ERR("Destinations vector wasn't created");
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
            fsync_src_threads_free(&pengine->src_threads);
            fsync_dst_threads_free(&pengine->dst_threads);
            fsync_engine_msgbus_release(pengine);

            if (pengine->stream_factory)
            {
                frstream_factory_istream_unsubscribe(pengine->stream_factory, (fristream_listener_t)fsync_engine_istream_listener);
                frstream_factory_release(pengine->stream_factory);
            }

            if(pengine->agents)
            {
                for(size_t i = 0; i < fvector_size(pengine->agents); ++i)
                {
                    fsync_agent_t *agent = *(fsync_agent_t**)fvector_at(pengine->agents, i);
                    agent->release(agent);
                }
                fvector_release(pengine->agents);
            }

            free(pengine);
        }
    }
    else
        FS_ERR("Invalid sync engine");
}

ferr_t fsync_engine_register_agent(fsync_engine_t *pengine, fsync_agent_t *agent)
{
    if (!pengine || !agent)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    return fsync_agent_add(pengine, agent);
}

ferr_t fsync_engine_sync(fsync_engine_t *pengine, fuuid_t const *dst, uint32_t agent_id, binn *metainf, fistream_t *pstream)
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
        return FFAIL;
    }

    sem_t sem = 0;

    if (sem_init(&sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        return FFAIL;
    }

    fsync_src_t src =
    {
        ++pengine->sync_id,
        *dst,
        agent_id,
        time(0),
        metainf ? binn_open(binn_ptr(metainf)) : 0,
        pstream
    };

    ferr_t ret = fsync_src_push_back(&pengine->src_threads, &src) ? FSUCCESS : FERR_NO_MEM;
    if (ret != FSUCCESS)
    {
        binn_free(src.metainf);
        sem_destroy(&sem);
        return ret;
    }

    return ret;
}
