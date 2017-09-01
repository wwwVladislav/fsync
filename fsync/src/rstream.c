#include "rstream.h"
#include <futils/log.h>
#include <futils/queue.h>
#include <futils/mutex.h>
#include <fcommon/messages.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

/*
 *          rstream
 *  src  -- FSTREAM ----------------------------> dst    mandatory
 *      <-- FSTREAM_ACCEPT / REJECT / FAILED ---         mandatory
 *      <-- FSTREAM_FAILED --------------------->        optional
 *      <-- FSTREAM_CLOSED --------------------->        optional
 *       -- FSTREAM_DATA ----------------------->        optional
*/

static struct timespec const F1_MSEC = { 0, 1000000 };
static struct timespec const F100_MSEC = { 0, 100000000 };
static const int FSTREAM_DATA_WAIT_ATTEMPTS = 30;       // 30 * F100_MSEC = 3 seconds

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// fristream
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
typedef struct
{
    fistream_t                  istream;        // base istream structure
    volatile uint32_t           ref_counter;    // References counter
    volatile fstream_status_t   status;         // status
    uint32_t                    id;             // stream id
    volatile uint64_t           read_size;      // read size
    volatile uint64_t           written_size;   // written size
    fuuid_t                     src;            // source address
    fuuid_t                     dst;            // destination address
    fmsgbus_t                  *msgbus;         // Messages bus
    pthread_mutex_t             mutex;          // Mutex for streams guard
    sem_t                       pin_sem;        // Semaphore for input stream data wait
    fistream_t                 *pin;            // input stream
    fostream_t                 *pout;           // output stream
} fristream_t;

static fristream_t     *fristream_retain (fristream_t *pstream);
static bool             fristream_release(fristream_t *pstream);
static size_t           fristream_read   (fristream_t *pstream, char *data, size_t size);
static void             fristream_close  (fristream_t *pstream);
static fstream_status_t fristream_status(fristream_t *pstream);

static void fristream_data_handler(fristream_t *pstream, FMSG_TYPE(stream_data) const *msg)
{
    if (pstream->status != FSTREAM_STATUS_OK)
        return;

    if (msg->stream_id != pstream->id
        || memcmp(&msg->hdr.src, &pstream->src, sizeof pstream->src) != 0)
        return;

    int i = 0;
    for(; msg->offset != pstream->written_size && i < FSTREAM_DATA_WAIT_ATTEMPTS; ++i)
        nanosleep(&F100_MSEC, NULL);

    if (i >= FSTREAM_DATA_WAIT_ATTEMPTS)
    {
        fristream_close(pstream);
        FS_ERR("Istream was closed. Data block wait timeout expired.");
    }
    else
    {
        size_t size = 0;

        fpush_lock(pstream->mutex);
        size = pstream->pout->write(pstream->pout, (char const *)msg->data, msg->size);
        fpop_lock();

        char src_str[2 * sizeof(fuuid_t) + 1] = { 0 };
        char dst_str[2 * sizeof(fuuid_t) + 1] = { 0 };
        FS_INFO("Received: %s<-%s offset=%llu, size=%u [%u B]",
                fuuid2str(&pstream->dst, dst_str, sizeof dst_str),
                fuuid2str(&pstream->src, src_str, sizeof src_str),
                pstream->written_size,
                msg->size,
                size);

        pstream->written_size += size;
        sem_post(&pstream->pin_sem);
    }
}

void fristream_reject_handler(fristream_t *pstream, FMSG_TYPE(stream_reject) const *msg)
{
    if (msg->stream_id != pstream->id
        || memcmp(&msg->hdr.src, &pstream->src, sizeof pstream->src) != 0)
        return;
    pstream->status = FSTREAM_STATUS_CLOSED;
}

void fristream_failed_handler(fristream_t *pstream, FMSG_TYPE(stream_failed) const *msg)
{
    if (msg->stream_id != pstream->id
        || memcmp(&msg->hdr.src, &pstream->src, sizeof pstream->src) != 0)
        return;
    pstream->status = FSTREAM_STATUS_INVALID;
}

void fristream_closed_handler(fristream_t *pstream, FMSG_TYPE(stream_closed) const *msg)
{
    if (msg->stream_id != pstream->id
        || memcmp(&msg->hdr.src, &pstream->src, sizeof pstream->src) != 0)
        return;
    pstream->status = FSTREAM_STATUS_CLOSED;
}

void fristream_node_disconnected_handler(fristream_t *pstream, FMSG_TYPE(node_disconnected) const *msg)
{
    if (memcmp(&msg->hdr.src, &pstream->src, sizeof pstream->src) != 0)
        return;
    pstream->status = FSTREAM_STATUS_CLOSED;
}

static void fristream_msgbus_retain(fristream_t *pstream, fmsgbus_t *pmsgbus)
{
    pstream->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(pmsgbus, FSTREAM_DATA,       (fmsg_handler_t)fristream_data_handler, pstream);
    fmsgbus_subscribe(pmsgbus, FSTREAM_REJECT,     (fmsg_handler_t)fristream_reject_handler,            pstream);
    fmsgbus_subscribe(pmsgbus, FSTREAM_FAILED,     (fmsg_handler_t)fristream_failed_handler,            pstream);
    fmsgbus_subscribe(pmsgbus, FSTREAM_CLOSED,     (fmsg_handler_t)fristream_closed_handler,            pstream);
    fmsgbus_subscribe(pmsgbus, FNODE_DISCONNECTED, (fmsg_handler_t)fristream_node_disconnected_handler, pstream);
}

static void fristream_msgbus_release(fristream_t *pstream)
{
    fmsgbus_unsubscribe(pstream->msgbus, FSTREAM_DATA,       (fmsg_handler_t)fristream_data_handler);
    fmsgbus_unsubscribe(pstream->msgbus, FSTREAM_REJECT,     (fmsg_handler_t)fristream_reject_handler);
    fmsgbus_unsubscribe(pstream->msgbus, FSTREAM_FAILED,     (fmsg_handler_t)fristream_failed_handler);
    fmsgbus_unsubscribe(pstream->msgbus, FSTREAM_CLOSED,     (fmsg_handler_t)fristream_closed_handler);
    fmsgbus_unsubscribe(pstream->msgbus, FNODE_DISCONNECTED, (fmsg_handler_t)fristream_node_disconnected_handler);
    fmsgbus_release(pstream->msgbus);
    pstream->msgbus = 0;
}

static fristream_t *fristream(fmsgbus_t *msgbus, uint32_t id, fuuid_t const *src, fuuid_t const *dst)
{
    fristream_t *pstream = (fristream_t*)malloc(sizeof(fristream_t));
    if (!pstream)
    {
        FS_ERR("Unable to allocate memory for istream");
        return 0;
    }
    memset(pstream, 0, sizeof *pstream);

    pstream->istream.retain  = (fistream_retain_fn_t)fristream_retain;
    pstream->istream.release = (fistream_release_fn_t)fristream_release;
    pstream->istream.read    = (fistream_read_fn_t)fristream_read;
    pstream->istream.status  = (fistream_status_fn_t)fristream_status;

    pstream->ref_counter = 1;
    pstream->status = FSTREAM_STATUS_OK;
    pstream->id = id;
    pstream->src = *src;
    pstream->dst = *dst;

    if (sem_init(&pstream->pin_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        fristream_release(pstream);
        return 0;
    }

    fristream_msgbus_retain(pstream, msgbus);

    pstream->mutex = PTHREAD_MUTEX_INITIALIZER;

    fmem_iostream_t *memstream = fmem_iostream(FMEM_BLOCK_SIZE);
    if (!memstream)
    {
        FS_ERR("Unable to allocate memory stream");
        fristream_release(pstream);
        return 0;
    }

    pstream->pin = fmem_istream(memstream);
    if (!pstream->pin)
    {
        FS_ERR("Unable to request istream interface for memory stream");
        fmem_iostream_release(memstream);
        fristream_release(pstream);
        return 0;
    }

    pstream->pout = fmem_ostream(memstream);
    if (!pstream->pout)
    {
        FS_ERR("Unable to request ostream interface for memory stream");
        fmem_iostream_release(memstream);
        fristream_release(pstream);
        return 0;
    }

    fmem_iostream_release(memstream);

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
            fristream_close(pstream);
            sem_destroy(&pstream->pin_sem);
            if (pstream->pin)
                pstream->pin->release(pstream->pin);
            if (pstream->pout)
                pstream->pout->release(pstream->pout);
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
    pstream->status = FSTREAM_STATUS_CLOSED;
    sem_post(&pstream->pin_sem);
    if (pstream->msgbus)
    {
        FMSG(stream_closed, closed, pstream->dst, pstream->src,
            pstream->id
        );
        if (fmsgbus_publish(pstream->msgbus, FSTREAM_CLOSED, (fmsg_t const *)&closed) != FSUCCESS)
            FS_ERR("Unable to publish stream close message");
        fristream_msgbus_release(pstream);
    }
}

static size_t fristream_read(fristream_t *pstream, char *data, size_t size)
{
    if (!pstream)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    if (pstream->status != FSTREAM_STATUS_OK)
    {
        FS_WARN("Istream is closed");
        return 0;
    }

    if (!data || !size)
        return 0;

    size_t read_size = 0;
    while (read_size < size)
    {
        while (sem_wait(&pstream->pin_sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        fpush_lock(pstream->mutex);
        read_size += pstream->pin->read(pstream->pin, data + read_size, size - read_size);
        fpop_lock();
    }

    pstream->read_size += read_size;

    sem_post(&pstream->pin_sem);

    return read_size;
}

static fstream_status_t fristream_status(fristream_t *pstream)
{
    if (!pstream)
    {
        FS_ERR("Invalid arguments");
        return FSTREAM_STATUS_INVALID;
    }
    return pstream->status;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// frostream
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
typedef struct
{
    fostream_t                  ostream;        // base ostream structure
    volatile uint32_t           ref_counter;    // References counter
    volatile fstream_status_t   status;         // status
    uint32_t                    id;             // stream id
    volatile uint64_t           written_size;   // written size
    fuuid_t                     src;            // source address
    fuuid_t                     dst;            // destination address
    fmsgbus_t                  *msgbus;         // Messages bus
} frostream_t;

static frostream_t     *frostream_retain (frostream_t *pstream);
static bool             frostream_release(frostream_t *pstream);
static size_t           frostream_write  (frostream_t *pstream, char const *data, size_t const size);
static fstream_status_t frostream_status (frostream_t *pstream);
static void             frostream_close  (frostream_t *pstream);

void frostream_accept_handler(frostream_t *pstream, FMSG_TYPE(stream_accept) const *msg)
{
    if (msg->stream_id != pstream->id
        || memcmp(&msg->hdr.src, &pstream->dst, sizeof pstream->dst) != 0)
        return;
    pstream->status = FSTREAM_STATUS_OK;
}

void frostream_reject_handler(frostream_t *pstream, FMSG_TYPE(stream_reject) const *msg)
{
    if (msg->stream_id != pstream->id
        || memcmp(&msg->hdr.src, &pstream->dst, sizeof pstream->dst) != 0)
        return;
    pstream->status = FSTREAM_STATUS_CLOSED;
}

void frostream_failed_handler(frostream_t *pstream, FMSG_TYPE(stream_failed) const *msg)
{
    if (msg->stream_id != pstream->id
        || memcmp(&msg->hdr.src, &pstream->dst, sizeof pstream->dst) != 0)
        return;
    pstream->status = FSTREAM_STATUS_INVALID;
}

void frostream_closed_handler(frostream_t *pstream, FMSG_TYPE(stream_closed) const *msg)
{
    if (msg->stream_id != pstream->id
        || memcmp(&msg->hdr.src, &pstream->dst, sizeof pstream->dst) != 0)
        return;
    pstream->status = FSTREAM_STATUS_CLOSED;
}

void frostream_node_disconnected_handler(frostream_t *pstream, FMSG_TYPE(node_disconnected) const *msg)
{
    if (memcmp(&msg->hdr.src, &pstream->dst, sizeof pstream->dst) != 0)
        return;
    pstream->status = FSTREAM_STATUS_CLOSED;
}

static void frostream_msgbus_retain(frostream_t *pstream, fmsgbus_t *pmsgbus)
{
    pstream->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(pmsgbus, FSTREAM_ACCEPT,     (fmsg_handler_t)frostream_accept_handler,            pstream);
    fmsgbus_subscribe(pmsgbus, FSTREAM_REJECT,     (fmsg_handler_t)frostream_reject_handler,            pstream);
    fmsgbus_subscribe(pmsgbus, FSTREAM_FAILED,     (fmsg_handler_t)frostream_failed_handler,            pstream);
    fmsgbus_subscribe(pmsgbus, FSTREAM_CLOSED,     (fmsg_handler_t)frostream_closed_handler,            pstream);
    fmsgbus_subscribe(pmsgbus, FNODE_DISCONNECTED, (fmsg_handler_t)frostream_node_disconnected_handler, pstream);
}

static void frostream_msgbus_release(frostream_t *pstream)
{
    fmsgbus_unsubscribe(pstream->msgbus, FSTREAM_ACCEPT,     (fmsg_handler_t)frostream_accept_handler);
    fmsgbus_unsubscribe(pstream->msgbus, FSTREAM_REJECT,     (fmsg_handler_t)frostream_reject_handler);
    fmsgbus_unsubscribe(pstream->msgbus, FSTREAM_FAILED,     (fmsg_handler_t)frostream_failed_handler);
    fmsgbus_unsubscribe(pstream->msgbus, FSTREAM_CLOSED,     (fmsg_handler_t)frostream_closed_handler);
    fmsgbus_unsubscribe(pstream->msgbus, FNODE_DISCONNECTED, (fmsg_handler_t)frostream_node_disconnected_handler);
    fmsgbus_release(pstream->msgbus);
    pstream->msgbus = 0;
}

static frostream_t *frostream(fmsgbus_t *msgbus, uint32_t id, fuuid_t const *src, fuuid_t const *dst)
{
    frostream_t *pstream = (frostream_t*)malloc(sizeof(frostream_t));
    if (!pstream)
    {
        FS_ERR("Unable to allocate memory for ostream");
        return 0;
    }
    memset(pstream, 0, sizeof *pstream);

    pstream->ostream.retain  = (fostream_retain_fn_t)frostream_retain;
    pstream->ostream.release = (fostream_release_fn_t)frostream_release;
    pstream->ostream.write   = (fostream_write_fn_t)frostream_write;
    pstream->ostream.status  = (fostream_status_fn_t)frostream_status;

    pstream->ref_counter = 1;
    pstream->status = FSTREAM_STATUS_INIT;
    pstream->id = id;
    pstream->src = *src;
    pstream->dst = *dst;
    frostream_msgbus_retain(pstream, msgbus);

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
            frostream_close(pstream);
            if (pstream->msgbus)
                frostream_msgbus_release(pstream);
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
    pstream->status = FSTREAM_STATUS_CLOSED;

    if (pstream->msgbus)
    {
        FMSG(stream_closed, closed, pstream->src, pstream->dst,
            pstream->id
        );
        if (fmsgbus_publish(pstream->msgbus, FSTREAM_CLOSED, (fmsg_t const *)&closed) != FSUCCESS)
            FS_ERR("Unable to publish stream close message");
        frostream_msgbus_release(pstream);
    }
}

static size_t frostream_write(frostream_t *pstream, char const *data, size_t const size)
{
    if (!pstream)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    if (pstream->status != FSTREAM_STATUS_OK)
    {
        FS_WARN("Ostream is closed");
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
        FS_INFO("Write: %s->%s offset=%llu, size=%u",
                fuuid2str(&pstream->src, src_str, sizeof src_str),
                fuuid2str(&pstream->dst, dst_str, sizeof dst_str),
                pstream->written_size,
                block_size);

        switch(fmsgbus_publish(pstream->msgbus, FSTREAM_DATA, (fmsg_t const *)&req))
        {
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

static fstream_status_t frostream_status(frostream_t *pstream)
{
    if (!pstream)
    {
        FS_ERR("Invalid arguments");
        return FSTREAM_STATUS_INVALID;
    }
    return pstream->status;
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
    FSTREAM_MSG = 0,
    FSTREAM_MSG_SUBSCRIBE_ISTREAM_LISTENER,
    FSTREAM_MSG_UNSUBSCRIBE_ISTREAM_LISTENER
};

typedef struct
{
    uint32_t                type;
    fuuid_t                 source;
    uint32_t                stream_id;
    uint32_t                metainf_size;
    uint8_t                 metainf[FMAX_METAINF_SIZE];
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
    fristream_listener_t    listener;
    sem_t                  *sem;
    ferr_t                 *ret;
} frstream_msg_unsubscribe_istream_listener_t;

typedef struct
{
    fristream_listener_t    listener;
    void                   *param;
} fristream_listener_info_t;

typedef struct
{
    volatile bool   is_active;
    pthread_t       thread;
} frstream_ctrl_thread_t;

struct frstream_factory
{
    volatile uint32_t           ref_counter;
    fuuid_t                     uuid;
    volatile uint32_t           stream_id;
    fmsgbus_t                  *msgbus;

    frstream_ctrl_thread_t      ctrl_thread;
    fristream_listener_info_t   ilisteners[FSTREAM_MAX_HANDLERS];
    pthread_mutex_t             messages_mutex;
    sem_t                       messages_sem;
    uint8_t                     buf[1024 * 1024];
    fring_queue_t              *messages;
};

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

static ferr_t frstream_factory_stream_msg_handler(frstream_factory_t *pfactory, frstream_msg_stream_t const *msg)
{
    for(int i = 0; i < FSTREAM_MAX_HANDLERS; ++i)
    {
        if (pfactory->ilisteners[i].listener)
        {
            fristream_t *pistream = fristream(pfactory->msgbus, msg->stream_id, &msg->source, &pfactory->uuid);
            if (!pistream) break;

            binn *obj = msg->metainf_size ? binn_open((void *)msg->metainf) : 0;

            frstream_info_t const rstream_info =
            {
                msg->source,
                obj
            };

            pfactory->ilisteners[i].listener(pfactory->ilisteners[i].param, (fistream_t*)pistream, &rstream_info);

            bool istream_is_released = fristream_release(pistream);
            if (obj) binn_free(obj);

            if (!istream_is_released)
            {
                char str[2 * sizeof(fuuid_t) + 1] = { 0 };
                FS_ERR("Input stream was accepted. Source: %s", fuuid2str(&msg->source, str, sizeof str));

                FMSG(stream_accept, accept, pfactory->uuid, msg->source,
                    msg->stream_id
                );
                if (fmsgbus_publish(pfactory->msgbus, FSTREAM_ACCEPT, (fmsg_t const *)&accept) != FSUCCESS)
                    FS_ERR("Unable to publish accept message");

                return FSUCCESS;
            }

            break;
        }
    }

    char str[2 * sizeof(fuuid_t) + 1] = { 0 };
    FS_ERR("Input stream wasn't accepted. There is no stream subscribers. Source: %s", fuuid2str(&msg->source, str, sizeof str));

    FMSG(stream_reject, reject, pfactory->uuid, msg->source,
        msg->stream_id
    );
    if (fmsgbus_publish(pfactory->msgbus, FSTREAM_REJECT, (fmsg_t const *)&reject) != FSUCCESS)
        FS_ERR("Unable to publish error message");

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
                case FSTREAM_MSG:
                {
                    frstream_msg_stream_t *msg = (frstream_msg_stream_t *)data;
                    if (frstream_factory_stream_msg_handler(pfactory, msg) != FSUCCESS)
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

                case FSTREAM_MSG_UNSUBSCRIBE_ISTREAM_LISTENER:
                {
                    frstream_msg_unsubscribe_istream_listener_t *msg = (frstream_msg_unsubscribe_istream_listener_t *)data;
                    *msg->ret = frstream_factory_unsubscribe_istream_listener_impl(pfactory, msg->listener);
                    sem_post(msg->sem);
                    fring_queue_pop_front(pfactory->messages);
                    break;
                }
            }
        }
    }

    return 0;
}

static void frstream_factory_stream_received(frstream_factory_t *pfactory, FMSG_TYPE(stream) const *msg)
{
    if (memcmp(&msg->hdr.dst, &pfactory->uuid, sizeof pfactory->uuid) == 0)
    {
        frstream_msg_stream_t stream_msg =
        {
            FSTREAM_MSG,
            msg->hdr.src,
            msg->stream_id,
            msg->metainf_size
        };
        memcpy(stream_msg.metainf, msg->metainf, msg->metainf_size);

        ferr_t ret;

        fpush_lock(pfactory->messages_mutex);
        ret = fring_queue_push_back(pfactory->messages, &stream_msg, sizeof stream_msg);
        fpop_lock();

        if (ret == FSUCCESS)
            sem_post(&pfactory->messages_sem);
        else
        {
            char str[2 * sizeof(fuuid_t) + 1] = { 0 };
            FS_ERR("Stream (src %s) handler failed", fuuid2str(&stream_msg.source, str, sizeof str));

            FMSG(stream_failed, err, pfactory->uuid, msg->hdr.src,
                msg->stream_id,
                ret,
                "There is no free space to handle the response"
            );
            if (fmsgbus_publish(pfactory->msgbus, FSTREAM_FAILED, (fmsg_t const *)&err) != FSUCCESS)
                FS_ERR("Unable to publish error message");
        }
    }
}

static void frstream_factory_msgbus_retain(frstream_factory_t *pfactory, fmsgbus_t *pmsgbus)
{
    pfactory->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(pmsgbus, FSTREAM, (fmsg_handler_t)frstream_factory_stream_received, pfactory);
}

static void frstream_factory_msgbus_release(frstream_factory_t *pfactory)
{
    fmsgbus_unsubscribe(pfactory->msgbus, FSTREAM, (fmsg_handler_t)frstream_factory_stream_received);
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
    pfactory->messages_mutex = PTHREAD_MUTEX_INITIALIZER;

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
fostream_t *frstream_factory_stream(frstream_factory_t *pfactory, fuuid_t const *dst, binn *metainf)
{
    if (!pfactory || !dst)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    uint32_t const metainf_size = metainf ? binn_size(metainf) : 0;
    if (metainf_size > FMAX_METAINF_SIZE)
    {
        FS_ERR("User data size is too long");
        return 0;
    }

    uint32_t const stream_id = ++pfactory->stream_id;

    frostream_t *postream = frostream(pfactory->msgbus, stream_id, &pfactory->uuid, dst);
    if (!postream) return 0;

    FMSG(stream, msg, pfactory->uuid, *dst,
        stream_id,
        metainf_size
    );
    if (metainf) memcpy(msg.metainf, binn_ptr(metainf), metainf_size);

    char src_str[2 * sizeof(fuuid_t) + 1] = { 0 };
    char dst_str[2 * sizeof(fuuid_t) + 1] = { 0 };
    FS_INFO("Stream was created. src=%s, dst=%s",
            fuuid2str(&pfactory->uuid, src_str, sizeof src_str),
            fuuid2str(dst, dst_str, sizeof dst_str));

    ferr_t rc = fmsgbus_publish(pfactory->msgbus, FSTREAM, (fmsg_t const *)&msg);
    if (rc != FSUCCESS)
    {
        FS_ERR("Stream request was failed");
        frostream_release(postream);
        postream = 0;
    }

    return (fostream_t *)postream;
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

    fpush_lock(pfactory->messages_mutex);
    ret = fring_queue_push_back(pfactory->messages, &msg, sizeof msg);
    fpop_lock();

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

    fpush_lock(pfactory->messages_mutex);
    ret = fring_queue_push_back(pfactory->messages, &msg, sizeof msg);
    fpop_lock();

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
