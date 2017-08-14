#include "rstream.h"
#include <futils/log.h>
#include <futils/queue.h>
#include <fcommon/messages.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

static struct timespec const F1_MSEC = { 0, 1000000 };

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// fristream
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
typedef struct
{
    fistream_t          istream;
    volatile uint32_t   ref_counter;
    volatile bool       is_open;
    uint32_t            id;
    fuuid_t             src;
    fmsgbus_t          *msgbus;
    fmem_iostream_t    *memstream;
} fristream_t;

static fristream_t *fristream_retain(fristream_t *pstream);
static bool         fristream_release(fristream_t *pstream);

static fristream_t *fristream(fmsgbus_t *msgbus, uint32_t id, fuuid_t const *src)
{
    fristream_t *pstream = (fristream_t*)malloc(sizeof(fristream_t));
    if (!pstream)
    {
        FS_ERR("Unable to allocate memory for istream");
        return 0;
    }
    memset(pstream, 0, sizeof *pstream);

    pstream->istream.retain = (fistream_retain_fn_t)fristream_retain;
    pstream->istream.release = (fistream_release_fn_t)fristream_release;

    pstream->ref_counter = 1;
    pstream->is_open = true;
    pstream->id = id;
    pstream->src = *src;
    pstream->msgbus = fmsgbus_retain(msgbus);
    pstream->memstream = fmem_iostream(FMEM_BLOCK_SIZE);

    if (!pstream->memstream)
    {
        FS_ERR("Unable to allocate memory stream");
        fristream_release(pstream);
        return 0;
    }

    return pstream;
}

static fristream_t *fristream_retain(fristream_t *pstream)
{
    if (pstream)
        pstream->ref_counter++;
    else
        FS_ERR("Invalid istream");
    return pstream;
}

static bool fristream_release(fristream_t *pstream)
{
    if (pstream)
    {
        if (!pstream->ref_counter)
            FS_ERR("Invalid istream");
        else if (!--pstream->ref_counter)
        {
            if (pstream->msgbus)
                fmsgbus_release(pstream->msgbus);
            if (pstream->memstream)
                fmem_iostream_release(pstream->memstream);
            memset(pstream, 0, sizeof *pstream);
            free(pstream);
            return true;
        }
    }
    else
        FS_ERR("Invalid istream");
    return false;
}

static void fristream_close(fristream_t *pstream)
{
    pstream->is_open = false;
    // TODO
}

//typedef size_t      (*fistream_read_fn_t)   (fistream_t *, char *, size_t);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// frostream
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
typedef struct
{
    fostream_t          ostream;
    volatile uint32_t   ref_counter;
    volatile bool       is_open;
    uint32_t            id;
    uint64_t            written_size;
    fuuid_t             src;
    fuuid_t             dst;
    fmsgbus_t          *msgbus;
} frostream_t;

static frostream_t *frostream_retain (frostream_t *pstream);
static bool         frostream_release(frostream_t *pstream);
static size_t       frostream_write  (frostream_t *pstream, char const *data, size_t const size);

static frostream_t *frostream(fmsgbus_t *msgbus, uint32_t id, fuuid_t const *src, fuuid_t const *dst)
{
    frostream_t *pstream = (frostream_t*)malloc(sizeof(frostream_t));
    if (!pstream)
    {
        FS_ERR("Unable to allocate memory for ostream");
        return 0;
    }
    memset(pstream, 0, sizeof *pstream);

    pstream->ostream.retain = (fostream_retain_fn_t)frostream_retain;
    pstream->ostream.release = (fostream_release_fn_t)frostream_release;
    pstream->ostream.write = (fostream_write_fn_t)frostream_write;

    pstream->ref_counter = 1;
    pstream->is_open = true;
    pstream->id = id;
    pstream->src = *src;
    pstream->dst = *dst;
    pstream->msgbus = fmsgbus_retain(msgbus);

    return pstream;
}

static frostream_t *frostream_retain(frostream_t *pstream)
{
    if (pstream)
        pstream->ref_counter++;
    else
        FS_ERR("Invalid ostream");
    return pstream;
}

static bool frostream_release(frostream_t *pstream)
{
    if (pstream)
    {
        if (!pstream->ref_counter)
            FS_ERR("Invalid ostream");
        else if (!--pstream->ref_counter)
        {
            if (pstream->msgbus)
                fmsgbus_release(pstream->msgbus);
            memset(pstream, 0, sizeof *pstream);
            free(pstream);
            return true;
        }
    }
    else
        FS_ERR("Invalid ostream");
    return false;
}

static void frostream_close(frostream_t *pstream)
{
    pstream->is_open = false;
}

static size_t frostream_write(frostream_t *pstream, char const *data, size_t const size)
{
    if (!pstream->is_open)
    {
        FS_WARN("Ostream is closed");
        return 0;
    }

    if (!pstream)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    if (!data || !size)
        return 0;

    size_t written_size = 0;

    while(written_size < size)
    {
        size_t const data_size = size - written_size;
        size_t const block_size = data_size >= FSYNC_BLOCK_SIZE ? FSYNC_BLOCK_SIZE : data_size;

        FMSG(stream_data, req, pstream->src, pstream->dst,
            pstream->id,
            pstream->written_size,
            block_size
        );
        memcpy(req.data, data + written_size, block_size);

        char src_str[2 * sizeof(fuuid_t) + 1] = { 0 };
        char dst_str[2 * sizeof(fuuid_t) + 1] = { 0 };
        FS_INFO("Write: %s->%s [%u B]", fuuid2str(&pstream->src, src_str, sizeof src_str), fuuid2str(&pstream->dst, dst_str, sizeof dst_str), block_size);

        switch(fmsgbus_publish(pstream->msgbus, FSTREAM_DATA, (fmsg_t const *)&req))
        {
            case FERR_NO_MEM:
                return written_size;
            case FSUCCESS:
                break;
            default:
                FS_ERR("Unable to write ostream data");
                frostream_close(pstream);
                return 0;
        }

        pstream->written_size += block_size;
        written_size += block_size;
    }

    return written_size;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// frstream factory
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

enum
{
    FSTREAM_MAX_HANDLERS = 256
};

enum
{
    FSTREAM_MSG_REQUEST = 0,
    FSTREAM_MSG,
    FSTREAM_MSG_SUBSCRIBE_ISTREAM_LISTENER,
    FSTREAM_MSG_SUBSCRIBE_OSTREAM_LISTENER,
    FSTREAM_MSG_UNSUBSCRIBE_ISTREAM_LISTENER,
    FSTREAM_MSG_UNSUBSCRIBE_OSTREAM_LISTENER
};

typedef struct
{
    uint32_t                type;
    fuuid_t                 source;
} frstream_msg_stream_request_t;

typedef struct
{
    uint32_t                type;
    fuuid_t                 dst;
    uint32_t                id;
} frstream_msg_stream_t;

typedef struct
{
    uint32_t                type;
    fristream_listener_t    listener;
    void                   *param;
    sem_t                  *sem;
    ferr_t                 *ret;
} frstream_msg_subscribe_istream_listener_t;

typedef struct
{
    uint32_t                type;
    frostream_listener_t    listener;
    void                   *param;
    sem_t                  *sem;
    ferr_t                 *ret;
} frstream_msg_subscribe_ostream_listener_t;

typedef struct
{
    uint32_t                type;
    fristream_listener_t    listener;
    sem_t                  *sem;
    ferr_t                 *ret;
} frstream_msg_unsubscribe_istream_listener_t;

typedef struct
{
    uint32_t                type;
    frostream_listener_t    listener;
    sem_t                  *sem;
    ferr_t                 *ret;
} frstream_msg_unsubscribe_ostream_listener_t;

typedef struct
{
    fristream_listener_t    listener;
    void                   *param;
} fristream_listener_info_t;

typedef struct
{
    frostream_listener_t    listener;
    void                   *param;
} frostream_listener_info_t;

typedef struct
{
    volatile bool   is_active;
    pthread_t       thread;
} frstream_ctrl_thread_t;

struct frstream_factory
{
    volatile uint32_t           ref_counter;
    fuuid_t                     uuid;
    volatile uint32_t           id;
    fmsgbus_t                  *msgbus;

    frstream_ctrl_thread_t      ctrl_thread;
    fristream_listener_info_t   ilisteners[FSTREAM_MAX_HANDLERS];
    frostream_listener_info_t   olisteners[FSTREAM_MAX_HANDLERS];
    pthread_mutex_t             messages_mutex;
    sem_t                       messages_sem;
    uint8_t                     buf[1024 * 1024];
    fring_queue_t              *messages;
};

#define frstream_factory_push_lock(mutex)           \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define frstream_factory_pop_lock() pthread_cleanup_pop(1)

static ferr_t frstream_factory_subscribe_istream_listener_impl(frstream_factory_t *pfactory, fristream_listener_t listener, void *param)
{
    int i = 0;

    for(; i < FSTREAM_MAX_HANDLERS; ++i)
    {
        if (!pfactory->ilisteners[i].listener)
        {
            pfactory->ilisteners[i].param = param;
            pfactory->ilisteners[i].listener = listener;
            break;
        }
    }

    if (i >= FSTREAM_MAX_HANDLERS)
    {
        FS_ERR("No free space in istream listeners table");
        return FFAIL;
    }

    return FSUCCESS;
}

static ferr_t frstream_factory_subscribe_ostream_listener_impl(frstream_factory_t *pfactory, frostream_listener_t listener, void *param)
{
    int i = 0;

    for(; i < FSTREAM_MAX_HANDLERS; ++i)
    {
        if (!pfactory->olisteners[i].listener)
        {
            pfactory->olisteners[i].param = param;
            pfactory->olisteners[i].listener = listener;
            break;
        }
    }

    if (i >= FSTREAM_MAX_HANDLERS)
    {
        FS_ERR("No free space in ostream listeners table");
        return FFAIL;
    }

    return FSUCCESS;
}

static ferr_t frstream_factory_unsubscribe_istream_listener_impl(frstream_factory_t *pfactory, fristream_listener_t listener)
{
    int i = 0;

    for(; i < FSTREAM_MAX_HANDLERS; ++i)
    {
        if (pfactory->ilisteners[i].listener == listener)
        {
            pfactory->ilisteners[i].param = 0;
            pfactory->ilisteners[i].listener = 0;
            break;
        }
    }

    if (i >= FSTREAM_MAX_HANDLERS)
    {
        FS_ERR("Istream listener not found in listeners table");
        return FFAIL;
    }

    return FSUCCESS;
}

static ferr_t frstream_factory_unsubscribe_ostream_listener_impl(frstream_factory_t *pfactory, frostream_listener_t listener)
{
    int i = 0;

    for(; i < FSTREAM_MAX_HANDLERS; ++i)
    {
        if (pfactory->olisteners[i].listener == listener)
        {
            pfactory->olisteners[i].param = 0;
            pfactory->olisteners[i].listener = 0;
            break;
        }
    }

    if (i >= FSTREAM_MAX_HANDLERS)
    {
        FS_ERR("Ostream listener not found in listeners table");
        return FFAIL;
    }

    return FSUCCESS;
}

static ferr_t frstream_factory_stream_request_impl(frstream_factory_t *pfactory, fuuid_t const *source)
{
    for(int i = 0; i < FSTREAM_MAX_HANDLERS; ++i)
    {
        if (pfactory->ilisteners[i].listener)
        {
            uint32_t const id = ++pfactory->id;
            fristream_t *pistream = fristream(pfactory->msgbus, id, source);
            if (!pistream) break;
            pfactory->ilisteners[i].listener(pfactory->ilisteners[i].param, (fistream_t*)pistream);
            fristream_release(pistream);

            // Send stream ID to source
            FMSG(stream, req, pfactory->uuid, *source,
                id
            );

            ferr_t rc = fmsgbus_publish(pfactory->msgbus, FSTREAM, (fmsg_t const *)&req);
            if (rc != FSUCCESS)
            {
                fristream_close(pistream);
                FS_ERR("Unable to publish stream");
            }

            return rc;
        }
    }

    char str[2 * sizeof(fuuid_t) + 1] = { 0 };
    FS_ERR("Input stream wasn't created. Source: %s", fuuid2str(source, str, sizeof str));
    return FFAIL;
}

static ferr_t frstream_factory_stream_response_impl(frstream_factory_t *pfactory, fuuid_t const *dst, uint32_t id)
{
    for(int i = 0; i < FSTREAM_MAX_HANDLERS; ++i)
    {
        if (pfactory->olisteners[i].listener)
        {
            frostream_t *postream = frostream(pfactory->msgbus, id, &pfactory->uuid, dst);
            if (!postream) break;
            pfactory->olisteners[i].listener(pfactory->olisteners[i].param, (fostream_t*)postream);
            frostream_release(postream);
            return FSUCCESS;
        }
    }

    char str[2 * sizeof(fuuid_t) + 1] = { 0 };
    FS_ERR("output stream wasn't created. Destination: %s", fuuid2str(dst, str, sizeof str));
    return FFAIL;
}

static void *frstream_factory_ctrl_thread(void *param)
{
    frstream_factory_t *pfactory = (frstream_factory_t *)param;
    frstream_ctrl_thread_t *thread = &pfactory->ctrl_thread;

    thread->is_active = true;

    while(thread->is_active)
    {
        while (sem_wait(&pfactory->messages_sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!thread->is_active)
            break;

        void *data;
        uint32_t size;

        if (fring_queue_front(pfactory->messages, &data, &size) == FSUCCESS)
        {
            switch(*(uint32_t *)data)
            {
                case FSTREAM_MSG_REQUEST:
                {
                    frstream_msg_stream_request_t *msg = (frstream_msg_stream_request_t *)data;
                    if (frstream_factory_stream_request_impl(pfactory, &msg->source) != FSUCCESS)
                    {
                        nanosleep(&F1_MSEC, NULL);
                        sem_post(&pfactory->messages_sem);
                    }
                    fring_queue_pop_front(pfactory->messages);
                    break;
                }

                case FSTREAM_MSG:
                {
                    frstream_msg_stream_t *msg = (frstream_msg_stream_t *)data;
                    if (frstream_factory_stream_response_impl(pfactory, &msg->dst, msg->id) != FSUCCESS)
                    {
                        nanosleep(&F1_MSEC, NULL);
                        sem_post(&pfactory->messages_sem);
                    }
                    fring_queue_pop_front(pfactory->messages);
                    break;
                }

                case FSTREAM_MSG_SUBSCRIBE_ISTREAM_LISTENER:
                {
                    frstream_msg_subscribe_istream_listener_t *msg = (frstream_msg_subscribe_istream_listener_t *)data;
                    *msg->ret = frstream_factory_subscribe_istream_listener_impl(pfactory, msg->listener, msg->param);
                    sem_post(msg->sem);
                    fring_queue_pop_front(pfactory->messages);
                    break;
                }

                case FSTREAM_MSG_SUBSCRIBE_OSTREAM_LISTENER:
                {
                    frstream_msg_subscribe_ostream_listener_t *msg = (frstream_msg_subscribe_ostream_listener_t *)data;
                    *msg->ret = frstream_factory_subscribe_ostream_listener_impl(pfactory, msg->listener, msg->param);
                    sem_post(msg->sem);
                    fring_queue_pop_front(pfactory->messages);
                    break;
                }

                case FSTREAM_MSG_UNSUBSCRIBE_ISTREAM_LISTENER:
                {
                    frstream_msg_unsubscribe_istream_listener_t *msg = (frstream_msg_unsubscribe_istream_listener_t *)data;
                    *msg->ret = frstream_factory_unsubscribe_istream_listener_impl(pfactory, msg->listener);
                    sem_post(msg->sem);
                    fring_queue_pop_front(pfactory->messages);
                    break;
                }

                case FSTREAM_MSG_UNSUBSCRIBE_OSTREAM_LISTENER:
                {
                    frstream_msg_unsubscribe_ostream_listener_t *msg = (frstream_msg_unsubscribe_ostream_listener_t *)data;
                    *msg->ret = frstream_factory_unsubscribe_ostream_listener_impl(pfactory, msg->listener);
                    sem_post(msg->sem);
                    fring_queue_pop_front(pfactory->messages);
                    break;
                }
            }
        }
    }

    return 0;
}

static void frstream_factory_stream_request(frstream_factory_t *pfactory, FMSG_TYPE(stream_request) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pfactory->uuid, sizeof pfactory->uuid) == 0)
    {
        frstream_msg_stream_request_t stream_request_msg =
        {
            FSTREAM_MSG_REQUEST,
            msg->hdr.src
        };

        ferr_t ret;

        frstream_factory_push_lock(pfactory->messages_mutex);
        ret = fring_queue_push_back(pfactory->messages, &stream_request_msg, sizeof stream_request_msg);
        frstream_factory_pop_lock();

        if (ret == FSUCCESS)
            sem_post(&pfactory->messages_sem);
        else
        {
            char str[2 * sizeof(fuuid_t) + 1] = { 0 };
            FS_ERR("Stream request (source %s) handler failed", fuuid2str(&stream_request_msg.source, str, sizeof str));
        }
    }
}

static void frstream_factory_stream(frstream_factory_t *pfactory, FMSG_TYPE(stream) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pfactory->uuid, sizeof pfactory->uuid) == 0)
    {
        frstream_msg_stream_t stream_msg =
        {
            FSTREAM_MSG,
            msg->hdr.src,
            msg->id
        };

        ferr_t ret;

        frstream_factory_push_lock(pfactory->messages_mutex);
        ret = fring_queue_push_back(pfactory->messages, &stream_msg, sizeof stream_msg);
        frstream_factory_pop_lock();

        if (ret == FSUCCESS)
            sem_post(&pfactory->messages_sem);
        else
        {
            char str[2 * sizeof(fuuid_t) + 1] = { 0 };
            FS_ERR("Stream (dst %s) handler failed", fuuid2str(&stream_msg.dst, str, sizeof str));
        }
    }
}

static void frstream_factory_msgbus_retain(frstream_factory_t *pfactory, fmsgbus_t *pmsgbus)
{
    pfactory->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(pmsgbus, FSTREAM_REQUEST, (fmsg_handler_t)frstream_factory_stream_request, pfactory);
    fmsgbus_subscribe(pmsgbus, FSTREAM,         (fmsg_handler_t)frstream_factory_stream,         pfactory);
}

static void frstream_factory_msgbus_release(frstream_factory_t *pfactory)
{
    fmsgbus_unsubscribe(pfactory->msgbus, FSTREAM_REQUEST, (fmsg_handler_t)frstream_factory_stream_request);
    fmsgbus_unsubscribe(pfactory->msgbus, FSTREAM,         (fmsg_handler_t)frstream_factory_stream);
    fmsgbus_release(pfactory->msgbus);
}

frstream_factory_t *frstream_factory(fmsgbus_t *pmsgbus, fuuid_t const *uuid)
{
    if (!pmsgbus || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    frstream_factory_t *pfactory = (frstream_factory_t *)malloc(sizeof(frstream_factory_t));
    if (!pfactory)
    {
        FS_ERR("Unable to allocate memory for rstream factory");
        return 0;
    }
    memset(pfactory, 0, sizeof *pfactory);

    pfactory->ref_counter = 1;
    pfactory->uuid = *uuid;

    static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
    pfactory->messages_mutex = mutex_initializer;

    if (sem_init(&pfactory->messages_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        frstream_factory_release(pfactory);
        return 0;
    }

    ferr_t ret = fring_queue_create(pfactory->buf, sizeof pfactory->buf, &pfactory->messages);
    if (ret != FSUCCESS)
    {
        frstream_factory_release(pfactory);
        return 0;
    }

    int rc = pthread_create(&pfactory->ctrl_thread.thread, 0, frstream_factory_ctrl_thread, pfactory);
    if (rc)
    {
        FS_ERR("Unable to create the thread. Error: %d", rc);
        frstream_factory_release(pfactory);
        return 0;
    }

    static struct timespec const ts = { 1, 0 };
    while(!pfactory->ctrl_thread.is_active)
        nanosleep(&ts, NULL);

    frstream_factory_msgbus_retain(pfactory, pmsgbus);

    return pfactory;
}

frstream_factory_t *frstream_factory_retain(frstream_factory_t *pfactory)
{
    if (pfactory)
        pfactory->ref_counter++;
    else
        FS_ERR("Invalid stream factory");
    return pfactory;
}

void frstream_factory_release(frstream_factory_t *pfactory)
{
    if (pfactory)
    {
        if (!pfactory->ref_counter)
            FS_ERR("Invalid stream factory");
        else if (!--pfactory->ref_counter)
        {
            pfactory->ctrl_thread.is_active = false;
            frstream_factory_msgbus_release(pfactory);
            sem_post(&pfactory->messages_sem);
            pthread_join(pfactory->ctrl_thread.thread, 0);
            sem_destroy(&pfactory->messages_sem);
            fring_queue_free(pfactory->messages);
            memset(pfactory, 0, sizeof *pfactory);
            free(pfactory);
        }
    }
    else
        FS_ERR("Invalid stream factory");
}

// src(ostream) ----> dst(istream)
ferr_t frstream_factory_ostream(frstream_factory_t *pfactory, fuuid_t const *dst)
{
    if (!pfactory || !dst)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    FMSG(stream_request, req, pfactory->uuid, *dst,);

    char src_str[2 * sizeof(fuuid_t) + 1] = { 0 };
    char dst_str[2 * sizeof(fuuid_t) + 1] = { 0 };
    FS_INFO("Requesting stream. src=%s, dst=%s", fuuid2str(&pfactory->uuid, src_str, sizeof src_str), fuuid2str(dst, dst_str, sizeof dst_str));

    ferr_t rc = fmsgbus_publish(pfactory->msgbus, FSTREAM_REQUEST, (fmsg_t const *)&req);
    if (rc != FSUCCESS)
        FS_ERR("Stream wasn't requested");

    return rc;
}

ferr_t frstream_factory_istream_subscribe(frstream_factory_t *pfactory, fristream_listener_t istream_listener, void *param)
{
    if (!pfactory || !istream_listener)
    {
        FS_ERR("Invalid arguments");
        return FFAIL;
    }

    ferr_t ret;
    sem_t  sem;
    ferr_t result = FSUCCESS;

    if (sem_init(&sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        return FFAIL;
    }

    frstream_msg_subscribe_istream_listener_t msg =
    {
        FSTREAM_MSG_SUBSCRIBE_ISTREAM_LISTENER,
        istream_listener,
        param,
        &sem,
        &result
    };

    frstream_factory_push_lock(pfactory->messages_mutex);
    ret = fring_queue_push_back(pfactory->messages, &msg, sizeof msg);
    frstream_factory_pop_lock();

    if (ret == FSUCCESS)
    {
        sem_post(&pfactory->messages_sem);

        // Waiting for completion
        struct timespec tm = { time(0) + 1, 0 };
        while(pfactory->ctrl_thread.is_active && sem_timedwait(&sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        ret = result;
    }

    sem_destroy(&sem);

    return ret;
}

ferr_t frstream_factory_ostream_subscribe(frstream_factory_t *pfactory, frostream_listener_t ostream_listener, void *param)
{
    if (!pfactory || !ostream_listener)
    {
        FS_ERR("Invalid arguments");
        return FFAIL;
    }

    ferr_t ret;
    sem_t  sem;
    ferr_t result = FSUCCESS;

    if (sem_init(&sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        return FFAIL;
    }

    frstream_msg_subscribe_ostream_listener_t msg =
    {
        FSTREAM_MSG_SUBSCRIBE_OSTREAM_LISTENER,
        ostream_listener,
        param,
        &sem,
        &result
    };

    frstream_factory_push_lock(pfactory->messages_mutex);
    ret = fring_queue_push_back(pfactory->messages, &msg, sizeof msg);
    frstream_factory_pop_lock();

    if (ret == FSUCCESS)
    {
        sem_post(&pfactory->messages_sem);

        // Waiting for completion
        struct timespec tm = { time(0) + 1, 0 };
        while(pfactory->ctrl_thread.is_active && sem_timedwait(&sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        ret = result;
    }

    sem_destroy(&sem);

    return ret;
}

ferr_t frstream_factory_istream_unsubscribe(frstream_factory_t *pfactory, fristream_listener_t istream_listener)
{
    if (!pfactory || !istream_listener)
    {
        FS_ERR("Invalid arguments");
        return FFAIL;
    }

    ferr_t ret;
    sem_t  sem;
    ferr_t result = FSUCCESS;

    if (sem_init(&sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        return FFAIL;
    }

    frstream_msg_unsubscribe_istream_listener_t msg =
    {
        FSTREAM_MSG_UNSUBSCRIBE_ISTREAM_LISTENER,
        istream_listener,
        &sem,
        &result
    };

    frstream_factory_push_lock(pfactory->messages_mutex);
    ret = fring_queue_push_back(pfactory->messages, &msg, sizeof msg);
    frstream_factory_pop_lock();

    if (ret == FSUCCESS)
    {
        sem_post(&pfactory->messages_sem);

        // Waiting for completion
        struct timespec tm = { time(0) + 1, 0 };
        while(pfactory->ctrl_thread.is_active && sem_timedwait(&sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        ret = result;
    }

    sem_destroy(&sem);

    return ret;
}

ferr_t frstream_factory_ostream_unsubscribe(frstream_factory_t *pfactory, frostream_listener_t ostream_listener)
{
    if (!pfactory || !ostream_listener)
    {
        FS_ERR("Invalid arguments");
        return FFAIL;
    }

    ferr_t ret;
    sem_t  sem;
    ferr_t result = FSUCCESS;

    if (sem_init(&sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        return FFAIL;
    }

    frstream_msg_unsubscribe_ostream_listener_t msg =
    {
        FSTREAM_MSG_UNSUBSCRIBE_OSTREAM_LISTENER,
        ostream_listener,
        &sem,
        &result
    };

    frstream_factory_push_lock(pfactory->messages_mutex);
    ret = fring_queue_push_back(pfactory->messages, &msg, sizeof msg);
    frstream_factory_pop_lock();

    if (ret == FSUCCESS)
    {
        sem_post(&pfactory->messages_sem);

        // Waiting for completion
        struct timespec tm = { time(0) + 1, 0 };
        while(pfactory->ctrl_thread.is_active && sem_timedwait(&sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        ret = result;
    }

    sem_destroy(&sem);

    return ret;
}