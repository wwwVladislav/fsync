#include "msgbus.h"
#include "queue.h"
#include "mutex.h"
#include "vector.h"
#include <fcommon/limits.h>
#include "log.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>
#include <errno.h>

static struct timespec const F10_MSEC = { 0, 10000000 };

typedef struct
{
    uint32_t        msg_type;
    fmsg_t         *msg;
} fmsgbus_msg_t;

typedef struct
{
    uint32_t        operation;
    uint32_t        msg_type;
    fmsg_handler_t  handler;
    void           *param;
    sem_t          *sem;
    ferr_t         *ret;
} fmsgbus_msg_subscribe_t;

typedef struct
{
    uint32_t        operation;
    uint32_t        msg_type;
    fmsg_handler_t  handler;
    sem_t          *sem;
    ferr_t         *ret;
} fmsgbus_msg_unsubscribe_t;

typedef struct
{
    volatile uint32_t   ref_counter;
    fmsg_handler_t      handler;
    void               *param;
} fmsgbus_handler_t;

typedef struct
{
    uint32_t            msg_type;
    fmsgbus_handler_t  *handler;
} fmsgbus_msg_handler_t;

typedef struct
{
    volatile bool       is_active;
    pthread_t           thread;
    fvector_t          *retained_handlers;  // retained handlers
} fmsgbus_thread_t;

typedef struct
{
    fmsgbus_t          *msgbus;
    uint32_t            thread_id;
} fmsgbus_thread_param_t;

struct fmsgbus
{
    volatile uint32_t     ref_counter;

    pthread_mutex_t       handlers_mutex;
    fvector_t            *handlers;         // vector of fmsgbus_msg_handler_t

    pthread_mutex_t       messages_mutex;
    sem_t                 messages_sem;
    uint8_t               buf[1024 * 1024];
    fring_queue_t        *messages;

    uint32_t              threads_num;
    fmsgbus_thread_t      threads[FMSGBUS_MAX_THREADS];
};

static fmsgbus_handler_t *fmsgbus_handler(fmsg_handler_t fn, void *param)
{
    fmsgbus_handler_t *handler = (fmsgbus_handler_t *)malloc(sizeof(fmsgbus_handler_t));
    if (!handler)
    {
        FS_ERR("No free space of memory for new message handler");
        return 0;
    }

    handler->ref_counter = 1;
    handler->handler = fn;
    handler->param = param;

    return handler;
}

static fmsgbus_handler_t *fmsgbus_handler_retain(fmsgbus_handler_t *handler)
{
    handler->ref_counter++;
    return handler;
}

static uint32_t fmsgbus_handler_release(fmsgbus_handler_t *handler)
{
    uint32_t rc = --handler->ref_counter;
    if (!rc)
    {
        memset(handler, 0, sizeof(*handler));
        free(handler);
    }
    return rc;
}

static void fmsgbus_handler_wait_last_ref(fmsgbus_handler_t *handler)
{
    while(handler->ref_counter > 1)
        nanosleep(&F10_MSEC, NULL);
}

static int fmsgbus_handlers_cmp(fmsgbus_msg_handler_t const *lhs, fmsgbus_msg_handler_t const *rhs)
{
    return (int)lhs->msg_type - rhs->msg_type;
}

static bool fmsgbus_handlers_range(fmsgbus_t *pmsgbus, uint32_t msg_type, size_t *first, size_t *last)
{
    fmsgbus_msg_handler_t const key = { msg_type, 0 };

    fmsgbus_msg_handler_t *msg_handler = (fmsgbus_msg_handler_t *)fvector_bsearch(pmsgbus->handlers, &key, (fvector_comparer_t)fmsgbus_handlers_cmp);
    if(!msg_handler)
        return false;

    size_t const size = fvector_size(pmsgbus->handlers);
    size_t const idx = fvector_idx(pmsgbus->handlers, msg_handler);

    *first = 0;
    *last = size;

    for(size_t i = idx; i < size; ++i)
    {
        msg_handler = (fmsgbus_msg_handler_t *)fvector_at(pmsgbus->handlers, i);
        if (msg_handler->msg_type != msg_type)
        {
            *last = i;
            break;
        }
    }

    for(long i = (long)idx - 1; i >= 0; --i)
    {
        msg_handler = (fmsgbus_msg_handler_t *)fvector_at(pmsgbus->handlers, i);
        if (msg_handler->msg_type != msg_type)
        {
            *first = i + 1;
            break;
        }
    }

    return true;
}

static bool fmsgbus_handlers_retain(fmsgbus_t *pmsgbus, uint32_t msg_type, fvector_t **handlers)
{
    bool ret = false;

    fpush_lock(pmsgbus->handlers_mutex);

    size_t first = 0, last = 0;

    if (fmsgbus_handlers_range(pmsgbus, msg_type, &first, &last))
    {
        for(size_t i = first; i < last; ++i)
        {
            fmsgbus_msg_handler_t *msg_handler = (fmsgbus_msg_handler_t *)fvector_at(pmsgbus->handlers, i);

            if (fvector_push_back(handlers, &msg_handler->handler))
            {
                fmsgbus_handler_retain(msg_handler->handler);
                ret = true;
            }
            else
                FS_ERR("Unable to retain message handler");
        }
    }

    fpop_lock();

    return ret;
}

static void fmsgbus_handlers_release(fvector_t **handlers)
{
    fmsgbus_handler_t **handlers_list = (fmsgbus_handler_t **)fvector_ptr(*handlers);
    size_t const size = fvector_size(*handlers);

    for(size_t i = 0; i < size; ++i)
    {
        fmsgbus_handler_t *handler = handlers_list[i];
        fmsgbus_handler_release(handler);
    }
    fvector_clear(handlers);
}

static ferr_t fmsgbus_subscribe_impl(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t fn, void *param)
{
    ferr_t ret = FFAIL;

    fmsgbus_handler_t *handler = fmsgbus_handler(fn, param);
    if(!handler)
        return FERR_NO_MEM;

    fpush_lock(pmsgbus->handlers_mutex);

    do
    {
        fmsgbus_msg_handler_t const msg_handler = { msg_type, handler };
        if (!fvector_push_back(&pmsgbus->handlers, &msg_handler))
        {
            FS_ERR("No free space in messages handlers table");
            fmsgbus_handler_release(handler);
            break;
        }

        fvector_qsort(pmsgbus->handlers, (fvector_comparer_t)fmsgbus_handlers_cmp);

        ret = FSUCCESS;
    }
    while(0);

    fpop_lock();

    return ret;
}

static ferr_t fmsgbus_unsubscribe_impl(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t fn)
{
    ferr_t ret = FSUCCESS;

    fmsgbus_handler_t *handler = 0;

    fpush_lock(pmsgbus->handlers_mutex);

    do
    {
        size_t first = 0, last = 0;

        if (!fmsgbus_handlers_range(pmsgbus, msg_type, &first, &last))
        {
            FS_WARN("Message handler not found in handlers table");
            break;
        }

        for(size_t i = first; i < last; ++i)
        {
            fmsgbus_msg_handler_t *msg_handler = (fmsgbus_msg_handler_t *)fvector_at(pmsgbus->handlers, i);
            if (msg_handler->handler->handler == fn)
            {
                msg_handler->handler->param = 0;
                handler = msg_handler->handler;

                if (fvector_erase(&pmsgbus->handlers, i))
                    break;

                FS_ERR("Message handler wasn't removed from handlers table");
                handler = 0;
                ret = FFAIL;
                break;
            }
        }
    }
    while(0);

    fpop_lock();

    if(handler)
    {
        fmsgbus_handler_wait_last_ref(handler);
        fmsgbus_handler_release(handler);
    }

    return ret;
}

static ferr_t fmsgbus_publish_impl(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_t const *msg)
{
    ferr_t ret;

    fmsgbus_msg_t const cmsg =
    {
        msg_type,
        malloc(msg->size)
    };

    if (!cmsg.msg)
    {
        FS_ERR("No free space of memory");
        return FERR_NO_MEM;
    }
    memcpy(cmsg.msg, msg, msg->size);

    fpush_lock(pmsgbus->messages_mutex);
    ret = fring_queue_push_back(pmsgbus->messages, &cmsg, sizeof cmsg);
    fpop_lock();

    if (ret == FSUCCESS)
        sem_post(&pmsgbus->messages_sem);
    else
        free(cmsg.msg);

    return ret;
}

static void fmsgbus_msg_handle(fmsgbus_t *msgbus, fvector_t *handlers, uint32_t msg_type, fmsg_t *msg)
{
    fmsgbus_handler_t **handlers_list = (fmsgbus_handler_t **)fvector_ptr(handlers);
    size_t const size = fvector_size(handlers);

    for(size_t i = 0; i < size; ++i)
    {
        fmsgbus_handler_t *handler = handlers_list[i];
        handler->handler(handler->param, msg);
    }
}

static void *fmsgbus_thread(void *param)
{
    fmsgbus_thread_param_t *thread_param = (fmsgbus_thread_param_t *)param;
    fmsgbus_t              *msgbus       = thread_param->msgbus;
    fmsgbus_thread_t       *thread       = &msgbus->threads[thread_param->thread_id];

    thread->is_active = true;

    while(thread->is_active)
    {
        while (sem_wait(&msgbus->messages_sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!thread->is_active)
            break;

        void *data = 0;
        uint32_t size = 0;
        fmsgbus_msg_t cmsg = { 0 };

        fpush_lock(msgbus->messages_mutex);
        if (fring_queue_front(msgbus->messages, &data, &size) == FSUCCESS)
        {
            cmsg = *(fmsgbus_msg_t *)data;
            fring_queue_pop_front(msgbus->messages);
        }
        fpop_lock();

        if (cmsg.msg)
        {
            if (thread->is_active)
            {
                if (fmsgbus_handlers_retain(msgbus, cmsg.msg_type, &thread->retained_handlers))
                {
                    fmsgbus_msg_handle(msgbus, thread->retained_handlers, cmsg.msg_type, cmsg.msg);
                    fmsgbus_handlers_release(&thread->retained_handlers);
                }
            }
            free(cmsg.msg);
        }
    }

    return 0;
}

ferr_t fmsgbus_create(fmsgbus_t **ppmsgbus, uint32_t threads_num)
{
    if (!ppmsgbus)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    if (!threads_num)
        threads_num = 1;
    else if (threads_num > FMSGBUS_MAX_THREADS)
        threads_num = FMSGBUS_MAX_THREADS;

    fmsgbus_t *pmsgbus = malloc(sizeof(fmsgbus_t));
    if (!pmsgbus)
    {
        FS_ERR("Unable to allocate memory for messages bus");
        return FERR_NO_MEM;
    }
    memset(pmsgbus, 0, sizeof(fmsgbus_t));

    pmsgbus->ref_counter = 1;
    pmsgbus->handlers_mutex = PTHREAD_MUTEX_INITIALIZER;
    pmsgbus->messages_mutex = PTHREAD_MUTEX_INITIALIZER;
    pmsgbus->threads_num = threads_num;

    pmsgbus->handlers = fvector(sizeof(fmsgbus_msg_handler_t), 0, 0);
    if (!pmsgbus->handlers)
    {
        FS_ERR("Messages handlers table initialization was failed");
        fmsgbus_release(pmsgbus);
        return FFAIL;
    }

    if (sem_init(&pmsgbus->messages_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization was failed");
        fmsgbus_release(pmsgbus);
        return FFAIL;
    }

    ferr_t ret = fring_queue_create(pmsgbus->buf, sizeof pmsgbus->buf, &pmsgbus->messages);
    if (ret != FSUCCESS)
    {
        fmsgbus_release(pmsgbus);
        return ret;
    }

    for(uint32_t i = 0; i < pmsgbus->threads_num; ++i)
    {
        fmsgbus_thread_t *thread = &pmsgbus->threads[i];
        fmsgbus_thread_param_t thread_param = { pmsgbus, i };

        thread->retained_handlers = fvector(sizeof(fmsgbus_handler_t *), 0, 0);
        if (!thread->retained_handlers)
        {
            FS_ERR("There is no free space of memory for messages handlers");
            fmsgbus_release(pmsgbus);
            return FFAIL;
        }

        int rc = pthread_create(&thread->thread, 0, fmsgbus_thread, &thread_param);
        if (rc)
        {
            FS_ERR("Unable to create the thread. Error: %d", rc);
            fmsgbus_release(pmsgbus);
            return FFAIL;
        }

        while(!pmsgbus->threads[i].is_active)
            nanosleep(&F10_MSEC, NULL);
    }

    *ppmsgbus = pmsgbus;

    return FSUCCESS;
}

fmsgbus_t *fmsgbus_retain(fmsgbus_t *pmsgbus)
{
    if (pmsgbus)
        pmsgbus->ref_counter++;
    else
        FS_ERR("Invalid message bus");
    return pmsgbus;
}

void fmsgbus_release(fmsgbus_t *pmsgbus)
{
    if (pmsgbus)
    {
        if (!pmsgbus->ref_counter)
            FS_ERR("Invalid message bus");
        else if (!--pmsgbus->ref_counter)
        {
            for(uint32_t i = 0; i < pmsgbus->threads_num; ++i)
                pmsgbus->threads[i].is_active = false;

            for(uint32_t i = 0; i < pmsgbus->threads_num; ++i)
                sem_post(&pmsgbus->messages_sem);
                
            for(uint32_t i = 0; i < pmsgbus->threads_num; ++i)
            {
                if(pmsgbus->threads[i].is_active)
                {
                    pthread_join(pmsgbus->threads[i].thread, 0);
                    fvector_release(pmsgbus->threads[i].retained_handlers);
                }
            }

            sem_destroy(&pmsgbus->messages_sem);
            fring_queue_free(pmsgbus->messages);
            fvector_release(pmsgbus->handlers);
            free(pmsgbus);
        }
    }
    else
        FS_ERR("Invalid message bus");
}

ferr_t fmsgbus_subscribe(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler, void *param)
{
    if (!pmsgbus
        || !handler)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }
    return fmsgbus_subscribe_impl(pmsgbus, msg_type, handler, param);
}

ferr_t fmsgbus_unsubscribe(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler)
{
    if (!pmsgbus
        || !handler)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }
    return fmsgbus_unsubscribe_impl(pmsgbus, msg_type, handler);
}

ferr_t fmsgbus_publish(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_t const *msg)
{
    if (!pmsgbus || !msg)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }
    return fmsgbus_publish_impl(pmsgbus, msg_type, msg);
}
